//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//

#include "yb/tablet/transaction_coordinator.h"

#include <atomic>
#include <iterator>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_round.h"
#include "yb/consensus/consensus_util.h"

#include "yb/docdb/transaction_dump.h"

#include "yb/gutil/stl_util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/poller.h"
#include "yb/rpc/rpc.h"

#include "yb/server/clock.h"

#include "yb/tablet/operations/update_txn_operation.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/tsan_util.h"
#include "yb/util/yb_pg_errcodes.h"

DECLARE_uint64(transaction_heartbeat_usec);
DEFINE_UNKNOWN_double(transaction_max_missed_heartbeat_periods, 10.0,
              "Maximum heartbeat periods that a pending transaction can miss before the "
              "transaction coordinator expires the transaction. The total expiration time in "
              "microseconds is transaction_heartbeat_usec times "
              "transaction_max_missed_heartbeat_periods. The value passed to this flag may be "
              "fractional.");
DEFINE_UNKNOWN_uint64(transaction_check_interval_usec, 500000,
    "Transaction check interval in usec.");
DEFINE_UNKNOWN_uint64(transaction_resend_applying_interval_usec, 5000000,
              "Transaction resend applying interval in usec.");
DEFINE_UNKNOWN_uint64(transaction_deadlock_detection_interval_usec, 60000000,
              "Deadlock detection interval in usec.");
TAG_FLAG(transaction_deadlock_detection_interval_usec, advanced);

DEFINE_UNKNOWN_int64(avoid_abort_after_sealing_ms, 20,
             "If transaction was only sealed, we will try to abort it not earlier than this "
                 "period in milliseconds.");

DEFINE_test_flag(uint64, inject_txn_get_status_delay_ms, 0,
                 "Inject specified delay to transaction get status requests.");
DEFINE_test_flag(int64, inject_random_delay_on_txn_status_response_ms, 0,
                 "Inject a random amount of delay to the thread processing a "
                 "GetTransactionStatusRequest after it has populated it's response. This could "
                 "help simulate e.g. out-of-order responses where PENDING is received by client "
                 "after a COMMITTED response.");

DEFINE_test_flag(bool, disable_cleanup_applied_transactions, false,
                 "Should we disable the GC of transactions already applied on all tablets.");

DEFINE_test_flag(bool, disable_apply_committed_transactions, false,
                 "Should we disable the apply of committed transactions.");

DECLARE_bool(enable_deadlock_detection);

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace tablet {

std::chrono::microseconds GetTransactionTimeout() {
  const double timeout = GetAtomicFlag(&FLAGS_transaction_max_missed_heartbeat_periods) *
                         GetAtomicFlag(&FLAGS_transaction_heartbeat_usec);
  // Cast to avoid -Wimplicit-int-float-conversion.
  return timeout >= static_cast<double>(std::chrono::microseconds::max().count())
      ? std::chrono::microseconds::max()
      : std::chrono::microseconds(static_cast<int64_t>(timeout));
}

namespace {

struct NotifyApplyingData {
  TabletId tablet;
  TransactionId transaction;
  AbortedSubTransactionSetPB aborted;
  HybridTime commit_time;
  bool sealed;
  bool is_external;
  // Only for external/xcluster transactions. How long to wait before retrying a failed apply
  // transaction.
  std::string ToString() const {
    return Format("{ tablet: $0 transaction: $1 commit_time: $2 sealed: $3 is_external $4}",
                  tablet, transaction, commit_time, sealed, is_external);
  }
};

struct ExpectedTabletBatches {
  TabletId tablet;
  size_t batches;

  std::string ToString() const {
    return Format("{ tablet: $0 batches: $1 }", tablet, batches);
  }
};

// Context for transaction state. I.e. access to external facilities required by
// transaction state to do its job.
class TransactionStateContext {
 public:
  virtual TransactionCoordinatorContext& coordinator_context() = 0;

  virtual void NotifyApplying(NotifyApplyingData data) = 0;

  // Submits update transaction to the RAFT log. Returns false if was not able to submit.
  virtual MUST_USE_RESULT bool SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperation> operation) = 0;

  virtual void CompleteWithStatus(
      std::unique_ptr<UpdateTxnOperation> request, Status status) = 0;

  virtual void CompleteWithStatus(UpdateTxnOperation* request, Status status) = 0;

  virtual bool leader() const = 0;

 protected:
  ~TransactionStateContext() {}
};

std::string BuildLogPrefix(const std::string& parent_log_prefix, const TransactionId& id) {
  auto id_string = id.ToString();
  return parent_log_prefix.substr(0, parent_log_prefix.length() - 2) + " ID " + id_string + ": ";
}

// TransactionState keeps state of single transaction.
// User of this class should guarantee that it does NOT invoke methods concurrently.
class TransactionState {
 public:
  explicit TransactionState(TransactionStateContext* context,
                            const TransactionId& id,
                            HybridTime last_touch,
                            const std::string& parent_log_prefix)
      : context_(*context),
        id_(id),
        log_prefix_(BuildLogPrefix(parent_log_prefix, id)),
        last_touch_(last_touch) {
  }

  ~TransactionState() {
    DCHECK(abort_waiters_.empty());
    DCHECK(request_queue_.empty());
    DCHECK(replicating_ == nullptr) << Format("Replicating: $0", static_cast<void*>(replicating_));
  }

  // Id of transaction.
  const TransactionId& id() const {
    return id_;
  }

  // Time when we last heard from transaction. I.e. hybrid time of replicated raft log entry
  // that updates status of this transaction.
  HybridTime last_touch() const {
    return last_touch_;
  }

  // Status of transaction.
  TransactionStatus status() const {
    return status_;
  }

  // RAFT index of first RAFT log entry required by this transaction.
  int64_t first_entry_raft_index() const {
    return first_entry_raft_index_;
  }

  bool is_external() const {
    return is_external_;
  }


  std::string ToString() const {
    return Format("{ id: $0 last_touch: $1 status: $2 involved_tablets: $3 replicating: $4 "
                      " request_queue: $5 first_entry_raft_index: $6 }",
                  id_, last_touch_, TransactionStatus_Name(status_),
                  involved_tablets_, replicating_, request_queue_, first_entry_raft_index_);
  }

  // Whether this transaction expired at specified time.
  bool ExpiredAt(HybridTime now) const {
    if (is_external()) {
      return false;
    }
    if (ShouldBeCommitted() || ShouldBeInStatus(TransactionStatus::SEALED)) {
      return false;
    }
    const int64_t passed = now.GetPhysicalValueMicros() - last_touch_.GetPhysicalValueMicros();
    if (std::chrono::microseconds(passed) > GetTransactionTimeout()) {
      return true;
    }
    return false;
  }

  // Whether this transaction has completed.
  bool Completed() const {
    return status_ == TransactionStatus::ABORTED ||
           status_ == TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;
  }

  // Applies new state to transaction.
  Status ProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    VLOG_WITH_PREFIX(4)
        << Format("ProcessReplicated: $0, replicating: $1", data, replicating_);

    if (replicating_ != nullptr) {
      auto* consensus_round = replicating_->consensus_round();
      if (!consensus_round) {
        LOG_WITH_PREFIX(DFATAL)
            << "Replicated an operation while the previous operation that was being replicated "
            << "did not even have a consensus round. Replicating: " << AsString(replicating_)
            << ", replicated: " << AsString(data);
      } else {
        auto replicating_op_id = consensus_round->id();
        if (!replicating_op_id.empty()) {
          if (replicating_op_id != data.op_id) {
            LOG_WITH_PREFIX(DFATAL)
                << "Replicated unexpected operation, replicating: " << AsString(replicating_)
                << ", replicated: " << AsString(data);
          }
        } else if (data.leader_term != OpId::kUnknownTerm) {
          LOG_WITH_PREFIX(DFATAL)
              << "Leader replicated operation without op id, replicating: "
              << AsString(replicating_) << ", replicated: " << AsString(data);
        } else {
          LOG_WITH_PREFIX(INFO) << "Cancel replicating without id: " << AsString(replicating_)
                                << ", because " << AsString(data) << " was replicated";
        }
      }
      replicating_ = nullptr;
    }

    auto status = DoProcessReplicated(data);

    if (data.leader_term == OpId::kUnknownTerm) {
      ClearRequests(STATUS(IllegalState, "Leader changed"));
    } else {
      switch(status_) {
        case TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
          ClearRequests(STATUS(AlreadyPresent, "Transaction committed"));
          break;
        case TransactionStatus::ABORTED:
          ClearRequests(
              STATUS(Expired, "Transaction aborted",
                     TransactionError(TransactionErrorCode::kAborted)));
          break;
        case TransactionStatus::CREATED: FALLTHROUGH_INTENDED;
        case TransactionStatus::PENDING: FALLTHROUGH_INTENDED;
        case TransactionStatus::SEALED: FALLTHROUGH_INTENDED;
        case TransactionStatus::COMMITTED: FALLTHROUGH_INTENDED;
        case TransactionStatus::PROMOTED: FALLTHROUGH_INTENDED;
        case TransactionStatus::APPLYING: FALLTHROUGH_INTENDED;
        case TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS: FALLTHROUGH_INTENDED;
        case TransactionStatus::IMMEDIATE_CLEANUP: FALLTHROUGH_INTENDED;
        case TransactionStatus::GRACEFUL_CLEANUP:
          ProcessQueue();
          break;
      }
    }

    return status;
  }

  void ProcessAborted(const TransactionCoordinator::AbortedData& data) {
    VLOG_WITH_PREFIX(4) << Format("ProcessAborted: $0, replicating: $1", data.state, replicating_);

    LOG_IF(DFATAL,
           replicating_ != nullptr && !replicating_->op_id().empty() &&
           replicating_->op_id() != data.op_id)
        << "Aborted wrong operation, expected " << AsString(replicating_) << ", but "
        << AsString(data) << " aborted";

    replicating_ = nullptr;

    // We are not leader, so could abort all queued requests.
    ClearRequests(STATUS(Aborted, "Replication failed"));
  }

  // Clear requests of this transaction.
  void ClearRequests(const Status& status) {
    VLOG_WITH_PREFIX(4) << Format("ClearRequests: $0, replicating: $1", status, replicating_);
    if (replicating_ != nullptr) {
      context_.CompleteWithStatus(replicating_, status);
      replicating_ = nullptr;
    }

    for (auto& entry : request_queue_) {
      context_.CompleteWithStatus(std::move(entry), status);
    }
    request_queue_.clear();

    NotifyAbortWaiters(status);
  }

  // Used only during transaction sealing.
  void ReplicatedAllBatchesAt(const TabletId& tablet, HybridTime last_time) {
    auto it = involved_tablets_.find(tablet);
    // We could be notified several times, so avoid double handling.
    if (it == involved_tablets_.end() || it->second.all_batches_replicated) {
      return;
    }

    // If transaction was sealed, then its commit time is max of seal record time and intent
    // replication times from all participating tablets.
    if (!is_external()) {
      commit_time_ = std::max(commit_time_, last_time);
    }
    --tablets_with_not_replicated_batches_;
    it->second.all_batches_replicated = true;

    if (tablets_with_not_replicated_batches_ == 0) {
      StartApply();
    }
  }

  const AbortedSubTransactionSetPB& GetAbortedSubTransactionSetPB() const { return aborted_; }

  Result<TransactionStatusResult> GetStatus(
      std::vector<ExpectedTabletBatches>* expected_tablet_batches) const {
    switch (status_) {
      case TransactionStatus::COMMITTED: FALLTHROUGH_INTENDED;
      case TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
        return TransactionStatusResult{TransactionStatus::COMMITTED, commit_time_};
      case TransactionStatus::SEALED:
        if (tablets_with_not_replicated_batches_ == 0) {
          return TransactionStatusResult{TransactionStatus::COMMITTED, commit_time_};
        }
        FillExpectedTabletBatches(expected_tablet_batches);
        return TransactionStatusResult{TransactionStatus::SEALED, commit_time_};
      case TransactionStatus::ABORTED:
        return TransactionStatusResult{TransactionStatus::ABORTED, HybridTime::kMax};
      case TransactionStatus::PENDING: {
        HybridTime status_ht;
        if (replicating_) {
          auto replicating_status = replicating_->request()->status();
          if (replicating_status == TransactionStatus::COMMITTED ||
              replicating_status == TransactionStatus::ABORTED) {
            auto replicating_ht = replicating_->hybrid_time_even_if_unset();
            if (replicating_ht.is_valid()) {
              status_ht = replicating_ht;
            } else {
              // Hybrid time now yet assigned to replicating, so assign more conservative time,
              // that is guaranteed to be less then replicating time. See GH #9981.
              status_ht = replicating_submit_time_;
            }
          }
        }
        if (!status_ht) {
          status_ht = context_.coordinator_context().clock().Now();
        }
        status_ht = std::min(status_ht, context_.coordinator_context().HtLeaseExpiration());
        return TransactionStatusResult{TransactionStatus::PENDING, status_ht.Decremented()};
      }
      case TransactionStatus::CREATED: FALLTHROUGH_INTENDED;
      case TransactionStatus::PROMOTED: FALLTHROUGH_INTENDED;
      case TransactionStatus::APPLYING: FALLTHROUGH_INTENDED;
      case TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS: FALLTHROUGH_INTENDED;
      case TransactionStatus::IMMEDIATE_CLEANUP: FALLTHROUGH_INTENDED;
      case TransactionStatus::GRACEFUL_CLEANUP:
        return STATUS_FORMAT(Corruption, "Transaction has unexpected status: $0",
                             TransactionStatus_Name(status_));
    }
    FATAL_INVALID_ENUM_VALUE(TransactionStatus, status_);
  }

  void Aborted() {
    status_ = TransactionStatus::ABORTED;
    NotifyAbortWaiters(TransactionStatusResult::Aborted());
  }

  TransactionStatusResult Abort(TransactionAbortCallback* callback) {
    if (status_ == TransactionStatus::COMMITTED ||
        status_ == TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS) {
      return TransactionStatusResult(TransactionStatus::COMMITTED, commit_time_);
    } else if (ShouldBeCommitted()) {
      return TransactionStatusResult(TransactionStatus::COMMITTED, HybridTime::kMax);
    } else if (status_ == TransactionStatus::ABORTED) {
      return TransactionStatusResult::Aborted();
    } else {
      VLOG_WITH_PREFIX(1) << "External abort request";
      CHECK_EQ(TransactionStatus::PENDING, status_);
      abort_waiters_.emplace_back(std::move(*callback));
      Abort();
      return TransactionStatusResult(TransactionStatus::PENDING, HybridTime::kMax);
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> request) {
    auto& state = *request->request();
    VLOG_WITH_PREFIX(1) << "Handle: " << state.ShortDebugString();
    if (state.status() == TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS) {
      auto status = AppliedInOneOfInvolvedTablets(state);
      context_.CompleteWithStatus(std::move(request), status);
      return;
    }
    if (replicating_) {
      request_queue_.push_back(std::move(request));
      return;
    }
    DoHandle(std::move(request));
  }

  // Aborts this transaction.
  void Abort() {
    if (ShouldBeCommitted()) {
      LOG_WITH_PREFIX(DFATAL) << "Transaction abort in wrong state: " << status_;
      return;
    }
    if (ShouldBeAborted()) {
      return;
    }
    if (status_ != TransactionStatus::PENDING) {
      LOG_WITH_PREFIX(DFATAL) << "Unexpected status during abort: "
                              << TransactionStatus_Name(status_);
      return;
    }
    SubmitUpdateStatus(TransactionStatus::ABORTED);
  }

  // Returns logs prefix for this transaction.
  const std::string& LogPrefix() {
    return log_prefix_;
  }

  // now_physical is just optimization to avoid querying the current time multiple times.
  void Poll(bool leader, MonoTime now_physical) {
    if (status_ != TransactionStatus::COMMITTED &&
        (status_ != TransactionStatus::SEALED || tablets_with_not_replicated_batches_ != 0)) {
      return;
    }
    if (tablets_with_not_applied_intents_ == 0) {
      if (leader && !ShouldBeInStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS)) {
        SubmitUpdateStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
      }
    } else if (now_physical >= resend_applying_time_) {
      if (leader) {
        for (auto& tablet : involved_tablets_) {
          if (!tablet.second.all_intents_applied) {
            context_.NotifyApplying({
                .tablet = tablet.first,
                .transaction = id_,
                .aborted = aborted_,
                .commit_time = commit_time_,
                .sealed = status_ == TransactionStatus::SEALED ,
                .is_external = is_external() });
          }
        }
      }
      resend_applying_time_ = now_physical +
          std::chrono::microseconds(FLAGS_transaction_resend_applying_interval_usec);
    }
  }

  void AddInvolvedTablets(
      const TabletId& source_tablet_id, const std::vector<TabletId>& tablet_ids) {
    auto source_it = involved_tablets_.find(source_tablet_id);
    if (source_it == involved_tablets_.end()) {
      LOG(FATAL) << "Unknown involved tablet: " << source_tablet_id;
      return;
    }
    for (const auto& tablet_id : tablet_ids) {
      if (involved_tablets_.emplace(tablet_id, source_it->second).second) {
        ++tablets_with_not_applied_intents_;
      }
    }
    if (!source_it->second.all_intents_applied) {
      // Mark source tablet as if intents have been applied for it.
      --tablets_with_not_applied_intents_;
      source_it->second.all_intents_applied = true;
    }
  }

  Status AppliedInOneOfInvolvedTablets(const LWTransactionStatePB& state) {
    if (state.tablets().size() != 1) {
      return STATUS_FORMAT(
          InvalidArgument, "Expected exactly one tablet in $0: $1", __func__, state);
    }

    return AppliedInOneOfInvolvedTablets(state.tablets().front());
  }

  Status AppliedInOneOfInvolvedTablets(const Slice& tablet_id) {
    if (status_ != TransactionStatus::COMMITTED && status_ != TransactionStatus::SEALED) {
      // We could ignore this request, because it will be re-sent if required.
      LOG_WITH_PREFIX(DFATAL)
          << "AppliedInOneOfInvolvedTablets in wrong state: " << TransactionStatus_Name(status_)
          << ", tablet: " << tablet_id.ToBuffer();
      return Status::OK();
    }

    auto it = involved_tablets_.find(std::string_view(tablet_id));
    if (it == involved_tablets_.end()) {
      // This can happen when transaction coordinator retried apply to post-split tablets,
      // transaction coordinator moved to new status tablet leader and here new transaction
      // coordinator receives notification about txn is applied in post-split tablet not yet known
      // to new transaction coordinator.
      // It is safe to just log warning and ignore, because new transaction coordinator is sending
      // again apply requests to all involved tablet it knows and will be retrying for ones that
      // will reply have been already split.
      LOG_WITH_PREFIX(WARNING) << "Applied in unknown tablet: " << tablet_id;
      return Status::OK();
    }
    if (!it->second.all_intents_applied) {
      --tablets_with_not_applied_intents_;
      it->second.all_intents_applied = true;
      VLOG_WITH_PREFIX(4) << "Applied to " << tablet_id << ", left not applied: "
                          << tablets_with_not_applied_intents_;
      if (tablets_with_not_applied_intents_ == 0) {
        SubmitUpdateStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
      }
    }
    return Status::OK();
  }

 private:
  // Checks whether we in specified status or going to be in this status when replication is
  // finished.
  bool ShouldBeInStatus(TransactionStatus status) const {
    if (status_ == status) {
      return true;
    }
    if (replicating_) {
      if (replicating_->request()->status() == status) {
        return true;
      }

      for (const auto& entry : request_queue_) {
        if (entry->request()->status() == status) {
          return true;
        }
      }
    }

    return false;
  }

  bool ShouldBeCommitted() const {
    return ShouldBeInStatus(TransactionStatus::COMMITTED) ||
           ShouldBeInStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
  }

  bool ShouldBeAborted() const {
    return ShouldBeInStatus(TransactionStatus::ABORTED);
  }

  // Process operation that was replicated in RAFT.
  Status DoProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    switch (data.state.status()) {
      case TransactionStatus::ABORTED:
        return AbortedReplicationFinished(data);
      case TransactionStatus::SEALED:
        return SealedReplicationFinished(data);
      case TransactionStatus::COMMITTED:
        return CommittedReplicationFinished(data);
      case TransactionStatus::CREATED: FALLTHROUGH_INTENDED;
      case TransactionStatus::PROMOTED: FALLTHROUGH_INTENDED;
      case TransactionStatus::PENDING:
        return PendingReplicationFinished(data);
      case TransactionStatus::APPLYING:
        // APPLYING is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
      case TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS:
        // APPLIED_IN_ONE_OF_INVOLVED_TABLETS handled w/o use of RAFT log
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
      case TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
        return AppliedInAllInvolvedTabletsReplicationFinished(data);
      case TransactionStatus::IMMEDIATE_CLEANUP: FALLTHROUGH_INTENDED;
      case TransactionStatus::GRACEFUL_CLEANUP:
        // CLEANUP is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
    }
    FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
  }

  void DoHandle(std::unique_ptr<tablet::UpdateTxnOperation> request) {
    const auto& state = *request->request();

    Status status;
    auto txn_status = state.status();
    if (txn_status == TransactionStatus::COMMITTED) {
      status = HandleCommit();
    } else if (txn_status == TransactionStatus::PENDING ||
               txn_status == TransactionStatus::CREATED) {
        // Handling txn_status of CREATED when the current status (status_) is PENDING is only
        // allowed for backward compatibility with versions prior to D11210, which could send
        // transaction creation retries with the same id.
      if (status_ != TransactionStatus::PENDING) {
        status = STATUS_FORMAT(IllegalState,
            "Transaction in wrong state during heartbeat: $0",
            TransactionStatus_Name(status_));
      }
    }

    if (!status.ok()) {
      context_.CompleteWithStatus(std::move(request), std::move(status));
      return;
    }

    VLOG_WITH_PREFIX(4) << Format("DoHandle, replicating = $0", replicating_);
    auto submitted = SubmitRequest(std::move(request));
    // Should always succeed, since we execute this code only on the leader.
    CHECK(submitted) << "Status: " << TransactionStatus_Name(txn_status);
  }

  Status HandleCommit() {
    auto hybrid_time = context_.coordinator_context().clock().Now();
    if (ExpiredAt(hybrid_time)) {
      auto status = STATUS(Expired, "Commit of expired transaction");
      VLOG_WITH_PREFIX(4) << status;
      Abort();
      return status;
    }
    if (status_ != TransactionStatus::PENDING) {
      return STATUS_FORMAT(IllegalState,
                           "Transaction in wrong state when starting to commit: $0",
                           TransactionStatus_Name(status_));
    }

    return Status::OK();
  }

  void SubmitUpdateStatus(TransactionStatus status) {
    VLOG_WITH_PREFIX(4) << "SubmitUpdateStatus(" << TransactionStatus_Name(status) << ")";

    auto state = rpc::MakeSharedMessage<LWTransactionStatePB>();
    state->dup_transaction_id(id_.AsSlice());
    state->set_status(status);

    auto request = context_.coordinator_context().CreateUpdateTransaction(std::move(state));
    if (replicating_) {
      request_queue_.push_back(std::move(request));
    } else {
      SubmitRequest(std::move(request));
    }
  }

  bool SubmitRequest(std::unique_ptr<tablet::UpdateTxnOperation> request) {
    replicating_ = request.get();
    replicating_submit_time_ = context_.coordinator_context().clock().Now();
    VLOG_WITH_PREFIX(4) << Format("SubmitUpdateStatus, replicating = $0", replicating_);
    if (!context_.SubmitUpdateTransaction(std::move(request))) {
      // Was not able to submit update transaction, for instance we are not leader.
      // So we are not replicating.
      replicating_ = nullptr;
      return false;
    }

    return true;
  }

  void ProcessQueue() {
    while (!replicating_ && !request_queue_.empty()) {
      auto request = std::move(request_queue_.front());
      request_queue_.pop_front();
      DoHandle(std::move(request));
    }
  }

  Status AbortedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    if (status_ != TransactionStatus::ABORTED &&
        status_ != TransactionStatus::PENDING) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid status of aborted transaction: "
                              << TransactionStatus_Name(status_);
    }

    status_ = TransactionStatus::ABORTED;
    first_entry_raft_index_ = data.op_id.index;
    NotifyAbortWaiters(TransactionStatusResult::Aborted());
    return Status::OK();
  }

  Status SealedReplicationFinished(
      const TransactionCoordinator::ReplicatedData& data) {
    if (status_ != TransactionStatus::PENDING) {
      auto status = STATUS_FORMAT(
          IllegalState,
          "Unexpected status during CommittedReplicationFinished: $0",
          TransactionStatus_Name(status_));
      LOG_WITH_PREFIX(DFATAL) << status;
      return status;
    }


    last_touch_ = data.hybrid_time;
    commit_time_ = data.hybrid_time;
    // TODO(dtxn) Not yet implemented
    next_abort_after_sealing_ = CoarseMonoClock::now() + FLAGS_avoid_abort_after_sealing_ms * 1ms;
    is_external_ = data.state.has_external_hybrid_time();

    // TODO(savepoints) Savepoints with sealed transactions is not yet tested
    data.state.aborted().ToGoogleProtobuf(&aborted_);
    VLOG_WITH_PREFIX(4) << "Seal time: " << commit_time_;
    status_ = TransactionStatus::SEALED;

    involved_tablets_.reserve(data.state.tablets().size());
    auto batches_it = data.state.tablet_batches().begin();
    for (const auto& tablet : data.state.tablets()) {
      auto tablet_batches = *batches_it++;
      LOG_IF_WITH_PREFIX(DFATAL, tablet_batches == 0)
          << "Tablet without batches: " << data.state.ShortDebugString();
      ++tablets_with_not_replicated_batches_;
      InvolvedTabletState state = {
        .required_replicated_batches = static_cast<size_t>(tablet_batches),
        .all_batches_replicated = false,
        .all_intents_applied = false
      };
      involved_tablets_.emplace(tablet.ToBuffer(), state);
    }

    first_entry_raft_index_ = data.op_id.index;
    return Status::OK();
  }

  Status CommittedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    if (status_ != TransactionStatus::PENDING) {
      auto status = STATUS_FORMAT(
          IllegalState,
          "Unexpected status during CommittedReplicationFinished: $0",
          TransactionStatus_Name(status_));
      LOG_WITH_PREFIX(DFATAL) << status;
      return status;
    }

    YB_TRANSACTION_DUMP(Commit, id_, data.hybrid_time, data.state.tablets().size());

    last_touch_ = data.hybrid_time;
    commit_time_ = data.hybrid_time;
    first_entry_raft_index_ = data.op_id.index;
    data.state.aborted().ToGoogleProtobuf(&aborted_);
    is_external_ = data.state.has_external_hybrid_time();

    involved_tablets_.reserve(data.state.tablets().size());
    for (const auto& tablet : data.state.tablets()) {
      InvolvedTabletState state = {
        .required_replicated_batches = 0,
        .all_batches_replicated = true,
        .all_intents_applied = false
      };
      involved_tablets_.emplace(tablet.ToBuffer(), state);
    }

    status_ = TransactionStatus::COMMITTED;
    StartApply();
    return Status::OK();
  }

  Status AppliedInAllInvolvedTabletsReplicationFinished(
      const TransactionCoordinator::ReplicatedData& data) {
    if (status_ != TransactionStatus::COMMITTED && status_ != TransactionStatus::SEALED) {
      // That could happen in old version, because we could drop all entries before
      // APPLIED_IN_ALL_INVOLVED_TABLETS.
      LOG_WITH_PREFIX(DFATAL)
          << "AppliedInAllInvolvedTabletsReplicationFinished in wrong state: "
          << TransactionStatus_Name(status_) << ", request: " << data.state.ShortDebugString();
      CHECK_EQ(status_, TransactionStatus::PENDING);
    }
    VLOG_WITH_PREFIX(4) << __func__ << ", status: " << TransactionStatus_Name(status_)
                        << ", leader: " << context_.leader();
    last_touch_ = data.hybrid_time;
    is_external_ = data.state.has_external_hybrid_time();
    status_ = TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;

    YB_TRANSACTION_DUMP(Applied, id_, data.hybrid_time);

    return Status::OK();
  }

  // Used for PENDING and CREATED records. Because when we apply replicated operations they have
  // the same meaning.
  Status PendingReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    if (context_.leader() && ExpiredAt(data.hybrid_time)) {
      VLOG_WITH_PREFIX(4) << "Expired during replication of PENDING or CREATED operations.";
      Abort();
      return Status::OK();
    }
    if (status_ != TransactionStatus::PENDING) {
      LOG_WITH_PREFIX(DFATAL) << "Bad status during " << __func__ << "(" << data.ToString()
                              << "): " << ToString();
      return Status::OK();
    }
    last_touch_ = data.hybrid_time;
    first_entry_raft_index_ = data.op_id.index;
    is_external_ = data.state.has_external_hybrid_time();

    // TODO(savepoints) -- consider swapping instead of copying here.
    // Asynchronous heartbeats don't include aborted sub-txn set (and hence the set is empty), so
    // avoid updating in those cases.
    if (!data.state.aborted().set().empty()) {
      data.state.aborted().ToGoogleProtobuf(&aborted_);
    }

    return Status::OK();
  }

  void NotifyAbortWaiters(const Result<TransactionStatusResult>& result) {
    for (auto& waiter : abort_waiters_) {
      waiter(result);
    }
    abort_waiters_.clear();
  }

  void StartApply() {
    VLOG_WITH_PREFIX(4) << __func__ << ", commit time: " << commit_time_ << ", involved tablets: "
                        << AsString(involved_tablets_);
    resend_applying_time_ = MonoTime::Now() +
        std::chrono::microseconds(FLAGS_transaction_resend_applying_interval_usec);
    tablets_with_not_applied_intents_ = involved_tablets_.size();
    if (context_.leader()) {
      for (const auto& tablet : involved_tablets_) {
        context_.NotifyApplying({
            .tablet = tablet.first,
            .transaction = id_,
            .aborted = aborted_,
            .commit_time = commit_time_,
            .sealed = status_ == TransactionStatus::SEALED,
            .is_external = is_external()});
      }
    }
    NotifyAbortWaiters(TransactionStatusResult(TransactionStatus::COMMITTED, commit_time_));
  }

  void FillExpectedTabletBatches(
      std::vector<ExpectedTabletBatches>* expected_tablet_batches) const {
    if (!expected_tablet_batches) {
      return;
    }

    for (const auto& tablet_id_and_state : involved_tablets_) {
      if (!tablet_id_and_state.second.all_batches_replicated) {
        expected_tablet_batches->push_back(ExpectedTabletBatches{
          .tablet = tablet_id_and_state.first,
          .batches = tablet_id_and_state.second.required_replicated_batches
        });
      }
    }
  }

  TransactionStateContext& context_;
  const TransactionId id_;
  const std::string log_prefix_;
  TransactionStatus status_ = TransactionStatus::PENDING;
  HybridTime last_touch_;
  // It should match last_touch_, but it is possible that because of some code errors it
  // would not be so. To add stability we introduce a separate field for it.
  HybridTime commit_time_;
  // If transaction was only sealed, we will try to abort it not earlier than this time.
  CoarseTimePoint next_abort_after_sealing_;
  // Is the transaction from xcluster.
  bool is_external_ = false;
  struct InvolvedTabletState {
    // How many batches should be replicated at this tablet.
    size_t required_replicated_batches = 0;

    // True if this tablet already replicated all batches.
    bool all_batches_replicated = false;

    // True if this tablet already applied all intents.
    bool all_intents_applied = false;

    std::string ToString() const {
      return Format("{ required_replicated_batches: $0 all_batches_replicated: $1 "
                        "all_intents_applied: $2 }",
                    required_replicated_batches, all_batches_replicated, all_intents_applied);
    }
  };

  // Tablets participating in this transaction.
  std::unordered_map<
      TabletId, InvolvedTabletState, StringHash, std::equal_to<void>> involved_tablets_;
  // Number of tablets that have not yet replicated all batches.
  size_t tablets_with_not_replicated_batches_ = 0;
  // Number of tablets that have not yet applied intents.
  size_t tablets_with_not_applied_intents_ = 0;
  // Don't resend applying until this time.
  MonoTime resend_applying_time_;
  int64_t first_entry_raft_index_ = std::numeric_limits<int64_t>::max();

  // Metadata tracking aborted subtransaction IDs in this transaction.
  AbortedSubTransactionSetPB aborted_;

  // The operation that we a currently replicating in RAFT.
  // It is owned by TransactionDriver (that will be renamed to OperationDriver).
  tablet::UpdateTxnOperation* replicating_ = nullptr;
  // Hybrid time before submitting replicating operation.
  // It is guaranteed to be less then actual operation hybrid time.
  HybridTime replicating_submit_time_;
  std::deque<std::unique_ptr<tablet::UpdateTxnOperation>> request_queue_;

  std::vector<TransactionAbortCallback> abort_waiters_;
};

struct CompleteWithStatusEntry {
  std::unique_ptr<UpdateTxnOperation> holder;
  UpdateTxnOperation* request;
  Status status;
};

// Contains actions that should be executed after lock in transaction coordinator is released.
struct PostponedLeaderActions {
  int64_t leader_term = OpId::kUnknownTerm;
  // List of tablets with transaction id, that should be notified that this transaction
  // is applying.
  std::vector<NotifyApplyingData> notify_applying;
  // List of update transaction records, that should be replicated via RAFT.
  std::vector<std::unique_ptr<UpdateTxnOperation>> updates;

  std::vector<CompleteWithStatusEntry> complete_with_status;

  void Swap(PostponedLeaderActions* other) {
    std::swap(leader_term, other->leader_term);
    notify_applying.swap(other->notify_applying);
    updates.swap(other->updates);
    complete_with_status.swap(other->complete_with_status);
  }

  bool leader() const {
    return leader_term != OpId::kUnknownTerm;
  }
};

} // namespace

std::string TransactionCoordinator::AbortedData::ToString() const {
  return YB_STRUCT_TO_STRING(state, op_id);
}

// Real implementation of transaction coordinator, as in PImpl idiom.
class TransactionCoordinator::Impl : public TransactionStateContext,
                                     public TransactionAbortController {
 public:
  Impl(const std::string& permanent_uuid,
       TransactionCoordinatorContext* context,
       Counter* expired_metric,
       const MetricEntityPtr& metrics)
      : context_(*context),
        expired_metric_(*expired_metric),
        log_prefix_(consensus::MakeTabletLogPrefix(context->tablet_id(), permanent_uuid)),
        deadlock_detector_(context->client_future(), this, context->tablet_id(), metrics),
        deadlock_detection_poller_(log_prefix_, std::bind(&Impl::PollDeadlockDetector, this)),
        poller_(log_prefix_, std::bind(&Impl::Poll, this)) {
  }

  virtual ~Impl() {
    Shutdown();
  }

  void Abort(const TransactionId& transaction_id, TransactionStatusCallback callback) override {
    Abort(transaction_id, context_.LeaderTerm(), callback);
  }

  void RemoveInactiveTransactions(Waiters* waiters) override {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    for (auto it = waiters->begin(); it != waiters->end();) {
      if (managed_transactions_.contains(it->first)) {
        ++it;
      } else {
        it = waiters->erase(it);
      }
    }
  }

  void Shutdown() {
    deadlock_detection_poller_.Shutdown();
    deadlock_detector_.Shutdown();
    poller_.Shutdown();
    rpcs_.Shutdown();
  }

  Status PrepareForDeletion(const CoarseTimePoint& deadline) {
    VLOG_WITH_PREFIX(4) << __func__;

    deleting_.store(true, std::memory_order_release);

    std::unique_lock<std::mutex> lock(managed_mutex_);
    if (!last_transaction_finished_.wait_until(
            lock, deadline, [this]() { return managed_transactions_.empty(); })) {
      return STATUS(TimedOut, "Timed out waiting for running transactions to complete");
    }

    return Status::OK();
  }

  Status GetStatus(const google::protobuf::RepeatedPtrField<std::string>& transaction_ids,
                   CoarseTimePoint deadline,
                   tserver::GetTransactionStatusResponsePB* response) {
    AtomicFlagSleepMs(&FLAGS_TEST_inject_txn_get_status_delay_ms);
    auto leader_term = context_.LeaderTerm();
    PostponedLeaderActions postponed_leader_actions;
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      HybridTime leader_safe_time;
      postponed_leader_actions_.leader_term = leader_term;
      for (const auto& transaction_id : transaction_ids) {
        auto id = VERIFY_RESULT(FullyDecodeTransactionId(transaction_id));

        auto it = managed_transactions_.find(id);
        std::vector<ExpectedTabletBatches> expected_tablet_batches;
        bool known_txn = it != managed_transactions_.end();
        auto txn_status_with_ht = known_txn
            ? VERIFY_RESULT(it->GetStatus(&expected_tablet_batches))
            : TransactionStatusResult(TransactionStatus::ABORTED, HybridTime::kMax);
        VLOG_WITH_PREFIX(4) << __func__ << ": " << id << " => " << txn_status_with_ht
                            << ", last touch: " << it->last_touch();
        if (txn_status_with_ht.status == TransactionStatus::SEALED) {
          // TODO(dtxn) Avoid concurrent resolve
          txn_status_with_ht = VERIFY_RESULT(ResolveSealedStatus(
              id, txn_status_with_ht.status_time, expected_tablet_batches,
              /* abort_if_not_replicated = */ false, &lock));
        }
        if (!known_txn) {
          if (!leader_safe_time) {
            // We should pick leader safe time only after managed_mutex_ is locked.
            // Otherwise applied transaction could be removed after this safe time.
            leader_safe_time = VERIFY_RESULT(context_.LeaderSafeTime());
          }
          // Please note that for known transactions we send 0, that means invalid hybrid time.
          // We would wait for safe time only for case when transaction is unknown to coordinator.
          // Since it is only case when transaction could be actually committed.
          response->mutable_coordinator_safe_time()->Resize(response->status().size(), 0);
          response->add_coordinator_safe_time(leader_safe_time.ToUint64());
        }
        response->add_status(txn_status_with_ht.status);
        response->add_status_hybrid_time(txn_status_with_ht.status_time.ToUint64());

        auto mutable_aborted_set_pb = response->add_aborted_subtxn_set();
        if (it != managed_transactions_.end() &&
            (txn_status_with_ht.status == TransactionStatus::COMMITTED ||
             txn_status_with_ht.status == TransactionStatus::PENDING)) {
          *mutable_aborted_set_pb = it->GetAbortedSubTransactionSetPB();
        }
      }
      postponed_leader_actions.Swap(&postponed_leader_actions_);
    }

    ExecutePostponedLeaderActions(&postponed_leader_actions);
    if (GetAtomicFlag(&FLAGS_TEST_inject_random_delay_on_txn_status_response_ms)) {
      if (response->status().size() > 0 && response->status(0) == TransactionStatus::PENDING) {
        AtomicFlagRandomSleepMs(&FLAGS_TEST_inject_random_delay_on_txn_status_response_ms);
      }
    }
    return Status::OK();
  }

  Result<TransactionStatusResult> ResolveSealedStatus(
      const TransactionId& transaction_id,
      HybridTime commit_time,
      const std::vector<ExpectedTabletBatches>& expected_tablet_batches,
      bool abort_if_not_replicated,
      std::unique_lock<std::mutex>* lock) {
    VLOG_WITH_PREFIX(4)
        << __func__ << ", txn: " << transaction_id << ", commit time: " << commit_time
        << ", expected tablet batches: " << AsString(expected_tablet_batches)
        << ", abort if not replicated: " << abort_if_not_replicated;

    auto deadline = TransactionRpcDeadline();
    auto now_ht = context_.clock().Now();
    CountDownLatch latch(expected_tablet_batches.size());
    std::vector<HybridTime> write_hybrid_times(expected_tablet_batches.size());
    {
      lock->unlock();
      auto scope_exit = ScopeExit([lock] {
        if (lock) {
          lock->lock();
        }
      });
      size_t idx = 0;
      for (const auto& p : expected_tablet_batches) {
        tserver::GetTransactionStatusAtParticipantRequestPB req;
        req.set_tablet_id(p.tablet);
        req.set_transaction_id(
            pointer_cast<const char*>(transaction_id.data()), transaction_id.size());
        req.set_propagated_hybrid_time(now_ht.ToUint64());
        if (abort_if_not_replicated) {
          req.set_required_num_replicated_batches(p.batches);
        }

        auto handle = rpcs_.Prepare();
        if (handle != rpcs_.InvalidHandle()) {
          *handle = GetTransactionStatusAtParticipant(
              deadline,
              nullptr /* remote_tablet */,
              context_.client_future().get(),
              &req,
              [this, handle, idx, &write_hybrid_times, &expected_tablet_batches, &latch,
               &transaction_id, &p](
                  const Status& status,
                  const tserver::GetTransactionStatusAtParticipantResponsePB& resp) {
                client::UpdateClock(resp, &context_);
                rpcs_.Unregister(handle);

                VLOG_WITH_PREFIX(4)
                    << "TXN: " << transaction_id << " batch status at " << p.tablet << ": "
                    << "idx: " << idx << ", resp: " << resp.ShortDebugString() << ", expected: "
                    << expected_tablet_batches[idx].batches;
                if (status.ok()) {
                  if (resp.aborted()) {
                    write_hybrid_times[idx] = HybridTime::kMin;
                  } else if (implicit_cast<size_t>(resp.num_replicated_batches()) ==
                                 expected_tablet_batches[idx].batches) {
                    write_hybrid_times[idx] = HybridTime(resp.status_hybrid_time());
                    LOG_IF_WITH_PREFIX(DFATAL, !write_hybrid_times[idx].is_valid())
                        << "Received invalid hybrid time when all batches were replicated: "
                        << resp.ShortDebugString();
                  }
                }
                latch.CountDown();
              });
          (**handle).SendRpc();
        } else {
          latch.CountDown();
        }
        ++idx;
      }
      latch.Wait();
    }

    auto txn_it = managed_transactions_.find(transaction_id);
    if (txn_it == managed_transactions_.end()) {
      // Transaction was completed (aborted/committed) during this procedure.
      return TransactionStatusResult{TransactionStatus::PENDING, commit_time.Decremented()};
    }

    for (size_t idx = 0; idx != expected_tablet_batches.size(); ++idx) {
      if (write_hybrid_times[idx] == HybridTime::kMin) {
        managed_transactions_.modify(txn_it, [](TransactionState& state) {
          state.Aborted();
        });
      } else if (write_hybrid_times[idx].is_valid()) {
        managed_transactions_.modify(
            txn_it, [idx, &expected_tablet_batches, &write_hybrid_times](TransactionState& state) {
          state.ReplicatedAllBatchesAt(
              expected_tablet_batches[idx].tablet, write_hybrid_times[idx]);
        });
      }
    }
    auto result = VERIFY_RESULT(txn_it->GetStatus(/* expected_tablet_batches = */ nullptr));
    if (result.status != TransactionStatus::SEALED) {
      VLOG_WITH_PREFIX(4) << "TXN: " << transaction_id << " status resolved: "
                          << TransactionStatus_Name(result.status);
      return result;
    }

    VLOG_WITH_PREFIX(4) << "TXN: " << transaction_id << " status NOT resolved";
    return TransactionStatusResult{TransactionStatus::PENDING, result.status_time.Decremented()};
  }

  void Abort(const std::string& transaction_id, int64_t term, TransactionAbortCallback callback) {
    AtomicFlagSleepMs(&FLAGS_TEST_inject_txn_get_status_delay_ms);

    auto id = FullyDecodeTransactionId(transaction_id);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "transaction_id: " << id << ".";
    if (!id.ok()) {
      callback(id.status());
      return;
    }
    Abort(*id, term, callback);
  }

  void Abort(const TransactionId& transaction_id, int64_t term, TransactionAbortCallback callback) {
    PostponedLeaderActions actions;
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      auto it = managed_transactions_.find(transaction_id);
      if (it == managed_transactions_.end()) {
        lock.unlock();
        VLOG_WITH_PREFIX_AND_FUNC(4) << "transaction_id: " << transaction_id << " not found.";
        callback(TransactionStatusResult::Aborted());
        return;
      }
      VLOG_WITH_PREFIX_AND_FUNC(4)
          << "transaction_id: " << transaction_id << " found, aborting now.";
      postponed_leader_actions_.leader_term = term;
      boost::optional<TransactionStatusResult> status;
      managed_transactions_.modify(it, [&status, &callback](TransactionState& state) {
        status = state.Abort(&callback);
      });
      if (callback) {
        lock.unlock();
        callback(*status);
        return;
      }
      actions.Swap(&postponed_leader_actions_);
    }

    ExecutePostponedLeaderActions(&actions);
  }

  size_t test_count_transactions() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    return managed_transactions_.size();
  }

  Status ProcessReplicated(const ReplicatedData& data) {
    auto id = FullyDecodeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      return std::move(id.status());
    }

    bool last_transaction = false;
    PostponedLeaderActions actions;
    Status result;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader_term = data.leader_term;
      auto it = GetTransaction(*id, data.state.status(), data.hybrid_time);
      if (it == managed_transactions_.end()) {
        return Status::OK();
      }
      managed_transactions_.modify(it, [&result, &data](TransactionState& state) {
        result = state.ProcessReplicated(data);
      });
      CheckCompleted(it);
      last_transaction = managed_transactions_.empty();
      actions.Swap(&postponed_leader_actions_);
    }
    if (last_transaction) {
      last_transaction_finished_.notify_one();
    }
    ExecutePostponedLeaderActions(&actions);

    VLOG_WITH_PREFIX(1) << "Processed: " << data.ToString();
    return result;
  }

  void ProcessAborted(const AbortedData& data) {
    auto id = FullyDecodeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Abort of transaction with bad id "
                              << data.state.ShortDebugString() << ": " << id.status();
      return;
    }

    bool last_transaction = false;
    PostponedLeaderActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader_term = OpId::kUnknownTerm;
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        LOG_WITH_PREFIX(WARNING) << "Aborted operation for unknown transaction: " << *id;
        return;
      }
      managed_transactions_.modify(
          it, [&](TransactionState& ts) {
            ts.ProcessAborted(data);
          });
      CheckCompleted(it);
      last_transaction = managed_transactions_.empty();
      actions.Swap(&postponed_leader_actions_);
    }
    if (last_transaction) {
      last_transaction_finished_.notify_one();
    }
    ExecutePostponedLeaderActions(&actions);

    VLOG_WITH_PREFIX(1) << "Aborted, state: " << data.state.ShortDebugString()
                        << ", op id: " << data.op_id;
  }

  void Start() {
    deadlock_detection_poller_.Start(
        &context_.client_future().get()->messenger()->scheduler(),
        1us * FLAGS_transaction_deadlock_detection_interval_usec * kTimeMultiplier);
    poller_.Start(
        &context_.client_future().get()->messenger()->scheduler(),
        1us * FLAGS_transaction_check_interval_usec * kTimeMultiplier);
  }

  Result<bool> MaybeIgnoreIfTransactionInWrongState(
      TransactionStatus request_txn_status, TransactionId transaction_id) {
    auto it = managed_transactions_.find(transaction_id);
    switch (request_txn_status) {
      case TransactionStatus::CREATED:
        // If the transaction is already present, then this CREATE record was already replicated at
        // some point in the past, so we can ignore this record.
        return it != managed_transactions_.end();
      case TransactionStatus::COMMITTED:
        // We ignore this COMMIT record if one of the following 2 conditions are met:
        // 1. The transaction doesn't exist and we're seeing a COMMIT record without a previous
        // CREATE. This means that at some time in the past, this transaction was already committed
        // and cleaned up, so ignore this this record.
        // 2. The transaction is present but not in CREATED or PENDING state. Because we only
        // replicate CREATED and COMMITTED records, if a transaction is present but not in CREATED
        // state, it must necessarily have already been committed.
        return it == managed_transactions_.end() ||
               (it->status() != TransactionStatus::CREATED &&
                it->status() != TransactionStatus::PENDING);
      default:
        return STATUS(IllegalState, Format("Request for unsupported external transaction state $0",
                                           request_txn_status));
    }
    return false;
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term) {
    auto& state = *request->request();
    auto id = FullyDecodeTransactionId(state.transaction_id());
    if (!id.ok()) {
      LOG(WARNING) << "Failed to decode id from " << state.ShortDebugString() << ": " << id;
      request->CompleteWithStatus(id.status());
      return;
    }

    if (state.has_external_hybrid_time()) {
      auto ignore_transaction_result =  MaybeIgnoreIfTransactionInWrongState(state.status(), *id);
      if (!ignore_transaction_result.ok()) {
        request->CompleteWithStatus(ignore_transaction_result.status());
        return;
      }
      if (*ignore_transaction_result) {
        request->CompleteWithStatus(Status::OK());
        return;
      }
    }

    PostponedLeaderActions actions;
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader_term = term;
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        auto status = HandleTransactionNotFound(*id, state);
        if (status.ok()) {
          it = managed_transactions_.emplace(
              this, *id, context_.clock().Now(), log_prefix_).first;
        } else {
          lock.unlock();
          status = status.CloneAndAddErrorCode(TransactionError(TransactionErrorCode::kAborted));
          request->CompleteWithStatus(status);
          return;
        }
      }

      managed_transactions_.modify(it, [&request](TransactionState& state) {
        state.Handle(std::move(request));
      });
      postponed_leader_actions_.Swap(&actions);
    }

    ExecutePostponedLeaderActions(&actions);
  }

  int64_t PrepareGC(std::string* details) {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    if (!managed_transactions_.empty()) {
      auto& txn = *managed_transactions_.get<FirstEntryIndexTag>().begin();
      if (details) {
        *details += Format("Transaction coordinator: $0\n", txn);
      }
      return txn.first_entry_raft_index();
    }
    return std::numeric_limits<int64_t>::max();
  }

  // Returns logs prefix for this transaction coordinator.
  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string DumpTransactions() {
    std::string result;
    std::lock_guard<std::mutex> lock(managed_mutex_);
    for (const auto& txn : managed_transactions_) {
      result += txn.ToString();
      result += "\n";
    }
    return result;
  }

  void ProcessWaitForReport(
      const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
      tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << req.ShortDebugString();

    if (!ANNOTATE_UNPROTECTED_READ(FLAGS_enable_deadlock_detection)) {
      YB_LOG_EVERY_N(WARNING, 100)
          << "Received wait-for report at node with deadlock detection disabled. "
          << "This should only happen during rolling restart.";
      callback(Status::OK());
    }

    return deadlock_detector_.ProcessWaitFor(req, resp, std::move(callback));
  }

  void ProcessProbe(
      const tserver::ProbeTransactionDeadlockRequestPB&req,
      tserver::ProbeTransactionDeadlockResponsePB* resp,
      DeadlockDetectorRpcCallback&& callback) {
    if (!ANNOTATE_UNPROTECTED_READ(FLAGS_enable_deadlock_detection)) {
      YB_LOG_EVERY_N(WARNING, 100)
          << "Received probe at node with deadlock detection disabled. "
          << "This should only happen during rolling restart.";
      return callback(Status::OK());
    }
    VLOG_WITH_PREFIX_AND_FUNC(4) << req.ShortDebugString();
    return deadlock_detector_.ProcessProbe(req, resp, std::move(callback));
  }

 private:
  class LastTouchTag;
  class FirstEntryIndexTag;

  typedef boost::multi_index_container<TransactionState,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<TransactionState,
                                                const TransactionId&,
                                                &TransactionState::id>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<LastTouchTag>,
              boost::multi_index::const_mem_fun<TransactionState,
                                                HybridTime,
                                                &TransactionState::last_touch>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<FirstEntryIndexTag>,
              boost::multi_index::const_mem_fun<TransactionState,
                                                int64_t,
                                                &TransactionState::first_entry_raft_index>
          >
      >
  > ManagedTransactions;

  void SendUpdateTransactionRequest(
      const NotifyApplyingData& action, HybridTime now,
      const CoarseTimePoint& deadline) {
    if (PREDICT_FALSE(FLAGS_TEST_disable_apply_committed_transactions)) {
      return;
    }
    VLOG_WITH_PREFIX(3) << "Notify applying: " << action.ToString();

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(action.tablet);
    req.set_propagated_hybrid_time(now.ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(action.transaction.data(), action.transaction.size());
    state.set_status(TransactionStatus::APPLYING);
    state.add_tablets(context_.tablet_id());
    state.set_commit_hybrid_time(action.commit_time.ToUint64());
    state.set_sealed(action.sealed);
    if (action.is_external) {
      req.set_is_external(true);
      state.set_external_hybrid_time(action.commit_time.ToUint64());
    }
    *state.mutable_aborted() = action.aborted;

    auto handle = rpcs_.Prepare();
    if (handle != rpcs_.InvalidHandle()) {
      *handle = UpdateTransaction(
          deadline,
          nullptr /* remote_tablet */,
          context_.client_future().get(),
          &req,
          [this, handle, action]
              (const Status& status,
               const tserver::UpdateTransactionRequestPB& req,
               const tserver::UpdateTransactionResponsePB& resp) {
            client::UpdateClock(resp, &context_);
            rpcs_.Unregister(handle);
            if (status.ok()) {
              return;
            }
            if (action.is_external && status.IsTryAgain()) {
              // We are trying to apply an external transaction on a tablet that is not caught up
              // to commit_ht. Return and let the Poll loop take care of the retry.
              return;
            }
            LOG_WITH_PREFIX(WARNING)
                << "Failed to send apply for transaction: " << action.transaction << ": "
                << status;
            const auto split_child_tablet_ids = SplitChildTabletIdsData(status).value();
            const bool tablet_has_been_split = !split_child_tablet_ids.empty();
            if (status.IsNotFound() || tablet_has_been_split) {
              std::lock_guard<std::mutex> lock(managed_mutex_);
              auto it = managed_transactions_.find(action.transaction);
              if (it == managed_transactions_.end()) {
                return;
              }
              managed_transactions_.modify(
                  it, [this, &action, &split_child_tablet_ids,
                       tablet_has_been_split](TransactionState& state) {
                    if (tablet_has_been_split) {
                      // We need to update involved tablets map.
                      LOG_WITH_PREFIX(INFO) << Format(
                          "Tablet $0 has been split into: $1", action.tablet,
                          split_child_tablet_ids);
                      state.AddInvolvedTablets(action.tablet, split_child_tablet_ids);
                    } else {
                      // Tablet has been deleted (not split), so we should mark it as applied to
                      // be able to cleanup the transaction.
                      WARN_NOT_OK(
                          state.AppliedInOneOfInvolvedTablets(action.tablet),
                          "AppliedInOneOfInvolvedTablets for removed tabled failed: ");
                    }
                  });
              if (tablet_has_been_split) {
                const auto new_deadline = TransactionRpcDeadline();
                NotifyApplyingData new_action = action;
                for (const auto& split_child_tablet_id : split_child_tablet_ids) {
                  new_action.tablet = split_child_tablet_id;
                  SendUpdateTransactionRequest(new_action, context_.clock().Now(), new_deadline);
                }
              }
            }
          });
      (**handle).SendRpc();
    }
  }

  void ExecutePostponedLeaderActions(PostponedLeaderActions* actions) {
    for (const auto& p : actions->complete_with_status) {
      p.request->CompleteWithStatus(p.status);
    }

    if (!actions->leader()) {
      return;
    }

    if (!actions->notify_applying.empty()) {
      auto now = context_.clock().Now();
      for (const auto& action : actions->notify_applying) {
        auto deadline = action.is_external ?
            ExternalTransactionRpcDeadline() : TransactionRpcDeadline();
        SendUpdateTransactionRequest(action, now, deadline);
      }
    }

    for (auto& update : actions->updates) {
      auto submit_status =
          context_.SubmitUpdateTransaction(std::move(update), actions->leader_term);
      if (!submit_status.ok()) {
        LOG_WITH_PREFIX(DFATAL)
            << "Could not submit transaction status update operation: "
            << update->ToString() << ", status: " << submit_status;
      }
    }
  }

  ManagedTransactions::iterator GetTransaction(const TransactionId& id,
                                               TransactionStatus status,
                                               HybridTime hybrid_time) {
    auto it = managed_transactions_.find(id);
    if (it == managed_transactions_.end()) {
      if (status != TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS) {
        it = managed_transactions_.emplace(this, id, hybrid_time, log_prefix_).first;
        VLOG_WITH_PREFIX(1) << Format("Added: $0", *it);
      }
    }
    return it;
  }

  Status HandleTransactionNotFound(const TransactionId& id,
                                   const LWTransactionStatePB& state) {
    if (state.status() != TransactionStatus::CREATED &&
        state.status() != TransactionStatus::PROMOTED) {
      YB_LOG_WITH_PREFIX_HIGHER_SEVERITY_WHEN_TOO_MANY(INFO, WARNING, 1s, 50)
          << "Request to unknown transaction " << id << ": "
          << state.ShortDebugString();
      return STATUS_EC_FORMAT(
          Expired, PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE),
          "Transaction $0 expired or aborted by a conflict", id);
    }

    if (deleting_.load(std::memory_order_acquire)) {
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1)
          << "Rejecting new transaction because status tablet is being deleted";
      return STATUS_FORMAT(
          Aborted, "Transaction $0 rejected because status tablet is being deleted", id);
    }

    return Status::OK();
  }

  TransactionCoordinatorContext& coordinator_context() override {
    return context_;
  }

  void NotifyApplying(NotifyApplyingData data) override {
    if (!leader()) {
      LOG_WITH_PREFIX(WARNING) << __func__ << " at non leader: " << data.ToString();
      return;
    }
    postponed_leader_actions_.notify_applying.push_back(std::move(data));
  }

  MUST_USE_RESULT bool SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperation> operation) override {
    if (!postponed_leader_actions_.leader()) {
      auto status = STATUS(IllegalState, "Submit update transaction on non leader");
      VLOG_WITH_PREFIX(1) << status;
      operation->CompleteWithStatus(status);
      return false;
    }

    postponed_leader_actions_.updates.push_back(std::move(operation));
    return true;
  }

  void CompleteWithStatus(
      std::unique_ptr<UpdateTxnOperation> request, Status status) override {
    auto ptr = request.get();
    postponed_leader_actions_.complete_with_status.push_back({
        std::move(request), ptr, std::move(status)});
  }

  void CompleteWithStatus(UpdateTxnOperation* request, Status status) override {
    postponed_leader_actions_.complete_with_status.push_back({
        nullptr /* holder */, request, std::move(status)});
  }

  bool leader() const override {
    return postponed_leader_actions_.leader();
  }

  void PollDeadlockDetector() {
    if (ANNOTATE_UNPROTECTED_READ(FLAGS_enable_deadlock_detection)) {
      deadlock_detector_.TriggerProbes();
    }
  }

  void Poll() {
    auto now = context_.clock().Now();

    auto leader_term = context_.LeaderTerm();
    bool leader = leader_term != OpId::kUnknownTerm;
    PostponedLeaderActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader_term = leader_term;

      auto& index = managed_transactions_.get<LastTouchTag>();

      if (VLOG_IS_ON(4) && leader && !index.empty()) {
        const auto& txn = *index.begin();
        LOG_WITH_PREFIX(INFO)
            << __func__ << ", now: " << now << ", first: " << txn.ToString()
            << ", expired: " << txn.ExpiredAt(now) << ", timeout: "
            << MonoDelta(GetTransactionTimeout()) << ", passed: "
            << MonoDelta::FromMicroseconds(
                   now.GetPhysicalValueMicros() - txn.last_touch().GetPhysicalValueMicros());
      }

      for (auto it = index.begin(); it != index.end() && it->ExpiredAt(now);) {
        if (it->status() == TransactionStatus::ABORTED) {
          it = index.erase(it);
        } else {
          if (leader) {
            expired_metric_.Increment();
            bool modified = index.modify(it, [](TransactionState& state) {
              VLOG(4) << state.LogPrefix() << "Cleanup expired transaction";
              state.Abort();
            });
            DCHECK(modified);
          }
          ++it;
        }
      }
      auto now_physical = MonoTime::Now();
      for (auto& transaction : managed_transactions_) {
        const_cast<TransactionState&>(transaction).Poll(leader, now_physical);
      }
      postponed_leader_actions_.Swap(&actions);
    }
    ExecutePostponedLeaderActions(&actions);
  }

  void CheckCompleted(ManagedTransactions::iterator it) {
    if (it->Completed()) {
      if (PREDICT_FALSE(FLAGS_TEST_disable_cleanup_applied_transactions)) {
        return;
      }
      auto status = STATUS_FORMAT(Expired, "Transaction completed: $0", *it);
      VLOG_WITH_PREFIX(1) << status;
      managed_transactions_.modify(it, [&status](TransactionState& state) {
        state.ClearRequests(status);
      });
      managed_transactions_.erase(it);
    }
  }

  TransactionCoordinatorContext& context_;
  Counter& expired_metric_;
  const std::string log_prefix_;

  std::mutex managed_mutex_;
  ManagedTransactions managed_transactions_;

  std::atomic<bool> deleting_{false};
  std::condition_variable last_transaction_finished_;

  // Actions that should be executed after mutex is unlocked.
  PostponedLeaderActions postponed_leader_actions_;

  DeadlockDetector deadlock_detector_;
  rpc::Poller deadlock_detection_poller_;

  rpc::Poller poller_;
  rpc::Rpcs rpcs_;
};

TransactionCoordinator::TransactionCoordinator(const std::string& permanent_uuid,
                                               TransactionCoordinatorContext* context,
                                               Counter* expired_metric,
                                               const MetricEntityPtr& metrics)
    : impl_(new Impl(permanent_uuid, context, expired_metric, metrics)) {
}

TransactionCoordinator::~TransactionCoordinator() {
}

Status TransactionCoordinator::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

void TransactionCoordinator::ProcessAborted(const AbortedData& data) {
  impl_->ProcessAborted(data);
}

int64_t TransactionCoordinator::PrepareGC(std::string* details) {
  return impl_->PrepareGC(details);
}

size_t TransactionCoordinator::test_count_transactions() const {
  return impl_->test_count_transactions();
}

void TransactionCoordinator::Handle(
    std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term) {
  impl_->Handle(std::move(request), term);
}

void TransactionCoordinator::Start() {
  impl_->Start();
}

void TransactionCoordinator::Shutdown() {
  impl_->Shutdown();
}

Status TransactionCoordinator::PrepareForDeletion(const CoarseTimePoint& deadline) {
  return impl_->PrepareForDeletion(deadline);
}

Status TransactionCoordinator::GetStatus(
    const google::protobuf::RepeatedPtrField<std::string>& transaction_ids,
    CoarseTimePoint deadline,
    tserver::GetTransactionStatusResponsePB* response) {
  return impl_->GetStatus(transaction_ids, deadline, response);
}

void TransactionCoordinator::Abort(const std::string& transaction_id,
                                   int64_t term,
                                   TransactionAbortCallback callback) {
  impl_->Abort(transaction_id, term, std::move(callback));
}

std::string TransactionCoordinator::DumpTransactions() {
  return impl_->DumpTransactions();
}

std::string TransactionCoordinator::ReplicatedData::ToString() const {
  return Format("{ leader_term: $0 state: $1 op_id: $2 hybrid_time: $3 txn_id: $4 }",
                leader_term, state, op_id, hybrid_time,
                FullyDecodeTransactionId(state.transaction_id()));
}

void TransactionCoordinator::ProcessWaitForReport(
    const tserver::UpdateTransactionWaitingForStatusRequestPB& req,
    tserver::UpdateTransactionWaitingForStatusResponsePB* resp,
    DeadlockDetectorRpcCallback&& callback) {
  return impl_->ProcessWaitForReport(req, resp, std::move(callback));
}

void TransactionCoordinator::ProcessProbe(
    const tserver::ProbeTransactionDeadlockRequestPB& req,
    tserver::ProbeTransactionDeadlockResponsePB* resp,
    DeadlockDetectorRpcCallback&& callback) {
  return impl_->ProcessProbe(req, resp, std::move(callback));
}

} // namespace tablet
} // namespace yb
