// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#pragma once

#include <stdint.h>

#include <future>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <mutex>

#include <boost/function.hpp>
#include <boost/functional/hash/hash.hpp>

#include <gtest/gtest_prod.h>

#include "yb/cdc/cdc_producer.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_fwd.h"

#include "yb/common/clock.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/retryable_request.h"
#include "yb/common/transaction.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_client.fwd.h"
#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_replication.fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_fwd.h"
#include "yb/util/status_callback.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/threadpool.h"

template<class T> class scoped_refptr;

namespace yb {

class CloudInfoPB;
class MemTracker;
class MetricEntity;

namespace cdc {
struct StreamMetaData;
}
namespace master {
class ReplicationInfoPB;
class TabletLocationsPB;
class GetAutoFlagsConfigResponsePB;
}

namespace tserver {
class LocalTabletServer;
class TabletServerServiceProxy;
}

namespace client {

namespace internal {
class ClientMasterRpcBase;
}

using GetTableLocationsCallback =
    std::function<void(const Result<master::GetTableLocationsResponsePB*>&)>;

using MasterAddressSource = std::function<std::vector<std::string>()>;

struct TransactionStatusTablets {
  std::vector<TabletId> global_tablets;
  std::vector<TabletId> placement_local_tablets;
};


// Creates a new YBClient with the desired options.
//
// Note that YBClients are shared amongst multiple threads and, as such,
// are stored in shared pointers.
class YBClientBuilder {
 public:
  YBClientBuilder();
  ~YBClientBuilder();

  YBClientBuilder& clear_master_server_addrs();

  // Add RPC addresses of multiple masters.
  YBClientBuilder& master_server_addrs(const std::vector<std::string>& addrs);

  // Add an RPC address of a master. At least one master is required.
  YBClientBuilder& add_master_server_addr(const std::string& addr);

  // Don't override master addresses with external information from FLAGS_flagfile.
  YBClientBuilder& skip_master_flagfile(bool should_skip = true);

  // The default timeout used for administrative operations (e.g. CreateTable,
  // AlterTable, ...). Optional.
  //
  // If not provided, defaults to 10s.
  YBClientBuilder& default_admin_operation_timeout(const MonoDelta& timeout);

  // The default timeout for individual RPCs. Optional.
  //
  // If not provided, defaults to 5s.
  YBClientBuilder& default_rpc_timeout(const MonoDelta& timeout);

  // Set the number of reactor threads that are used to send out the requests.
  // (defaults to the flag value yb_client_num_reactors : 16).
  YBClientBuilder& set_num_reactors(int32_t num_reactors);

  // Sets the cloud info for the client, indicating where the client is located.
  YBClientBuilder& set_cloud_info_pb(const CloudInfoPB& cloud_info_pb);

  // Sets metric entity to be used for emitting metrics. Optional.
  YBClientBuilder& set_metric_entity(const scoped_refptr<MetricEntity>& metric_entity);

  // Sets client name to be used for naming the client's messenger/reactors.
  YBClientBuilder& set_client_name(const std::string& name);

  // Sets the size of the threadpool for calling callbacks.
  YBClientBuilder& set_callback_threadpool_size(size_t size);

  YBClientBuilder& wait_for_leader_election_on_init(bool should_wait = true);

  // Sets skip master leader resolution.
  // Used in tests, when we do not have real master.
  YBClientBuilder& set_skip_master_leader_resolution(bool value);

  // Sets the tserver uuid for the client used by the CQL proxy. Intended only for use by CQL
  // proxy clients.
  YBClientBuilder& set_tserver_uuid(const TabletServerId& uuid);

  YBClientBuilder& set_parent_mem_tracker(const std::shared_ptr<MemTracker>& mem_tracker);

  YBClientBuilder& set_master_address_flag_name(const std::string& value);

  YBClientBuilder& AddMasterAddressSource(const MasterAddressSource& source);

  // Creates the client.
  // Will use specified messenger if not nullptr.
  // If messenger is nullptr - messenger will be created and owned by client. Client will shutdown
  // messenger on client shutdown.
  //
  // The return value may indicate an error in the create operation, or a
  // misuse of the builder; in the latter case, only the last error is
  // returned.
  Result<std::unique_ptr<YBClient>> Build(rpc::Messenger* messenger = nullptr);

  // Creates the client which gets the messenger ownership and shuts it down on client shutdown.
  Result<std::unique_ptr<YBClient>> Build(std::unique_ptr<rpc::Messenger>&& messenger);

 private:
  class Data;

  Status DoBuild(rpc::Messenger* messenger, std::unique_ptr<client::YBClient>* client);

  std::unique_ptr<Data> data_;

  DISALLOW_COPY_AND_ASSIGN(YBClientBuilder);
};

// The YBClient represents a connection to a cluster. From the user
// perspective, they should only need to create one of these in their
// application, likely a singleton -- but it's not a singleton in YB in any
// way. Different Client objects do not interact with each other -- no
// connection pooling, etc. Each YBClient instance is sandboxed with no
// global cross-client state.
//
// In the implementation, the client holds various pieces of common
// infrastructure which is not table-specific:
//
// - RPC messenger: reactor threads and RPC connections are pooled here
// - Authentication: the client is initialized with some credentials, and
//   all accesses through it share those credentials.
// - Caches: caches of table schemas, tablet locations, tablet server IP
//   addresses, etc are shared per-client.
//
// In order to actually access data on the cluster, callers must first
// create a YBSession object using NewSession(). A YBClient may
// have several associated sessions.
//
// TODO: Cluster administration functions are likely to be in this class
// as well.
//
// This class is thread-safe.
class YBClient {
 public:
  ~YBClient();

  std::unique_ptr<YBTableCreator> NewTableCreator();

  // set 'create_in_progress' to true if a CreateTable operation is in-progress.
  Status IsCreateTableInProgress(const YBTableName& table_name,
                                 bool *create_in_progress);

  // Wait for create table to finish.
  Status WaitForCreateTableToFinish(const YBTableName& table_name);
  Status WaitForCreateTableToFinish(const YBTableName& table_name,
                                    const CoarseTimePoint& deadline);

  Status WaitForCreateTableToFinish(const std::string& table_id);
  Status WaitForCreateTableToFinish(const std::string& table_id,
                                    const CoarseTimePoint& deadline);

  // Wait for delete table to finish.
  Status WaitForDeleteTableToFinish(const std::string& table_id);
  Status WaitForDeleteTableToFinish(const std::string& table_id,
                                    const CoarseTimePoint& deadline);

  // Truncate the specified table.
  // Set 'wait' to true if the call must wait for the table to be fully truncated before returning.
  Status TruncateTable(const std::string& table_id, bool wait = true);
  Status TruncateTables(const std::vector<std::string>& table_ids, bool wait = true);

  // Backfill the specified index table.  This is only supported for YSQL at the moment.
  Status BackfillIndex(const TableId& table_id, bool wait = true,
                       CoarseTimePoint deadline = CoarseTimePoint());

  // Delete the specified table.
  // Set 'wait' to true if the call must wait for the table to be fully deleted before returning.
  Status DeleteTable(const YBTableName& table_name, bool wait = true);
  // 'txn' describes the transaction that is performing this delete operation. For YSQL
  // operations, YB-Master will perform the actual deletion only if this transaction is a
  // success.
  Status DeleteTable(const std::string& table_id,
                     bool wait = true,
                     const TransactionMetadata *txn = nullptr,
                     CoarseTimePoint deadline = CoarseTimePoint());

  // Delete the specified index table.
  // Set 'wait' to true if the call must wait for the table to be fully deleted before returning.
  Status DeleteIndexTable(const YBTableName& table_name,
                          YBTableName* indexed_table_name = nullptr,
                          bool wait = true);

  Status DeleteIndexTable(const std::string& table_id,
                          YBTableName* indexed_table_name = nullptr,
                          bool wait = true,
                          CoarseTimePoint deadline = CoarseTimePoint());

  // Flush or compact the specified tables.
  Status FlushTables(const std::vector<TableId>& table_ids,
                     bool add_indexes,
                     int timeout_secs,
                     bool is_compaction);
  Status FlushTables(const std::vector<YBTableName>& table_names,
                     bool add_indexes,
                     int timeout_secs,
                     bool is_compaction);

  std::unique_ptr<YBTableAlterer> NewTableAlterer(const YBTableName& table_name);
  std::unique_ptr<YBTableAlterer> NewTableAlterer(const std::string id);

  // Set 'alter_in_progress' to true if an AlterTable operation is in-progress.
  Status IsAlterTableInProgress(const YBTableName& table_name,
                                const std::string& table_id,
                                bool *alter_in_progress);

  Status GetTableSchema(const YBTableName& table_name,
                        YBSchema* schema,
                        PartitionSchema* partition_schema);
  Status GetYBTableInfo(const YBTableName& table_name, std::shared_ptr<YBTableInfo> info,
                        StatusCallback callback);
  Result<YBTableInfo> GetYBTableInfo(const YBTableName& table_name);

  Status GetTableSchemaById(const TableId& table_id, std::shared_ptr<YBTableInfo> info,
                            StatusCallback callback);

  Status GetTablegroupSchemaById(const TablegroupId& tablegroup_id,
                                 std::shared_ptr<std::vector<YBTableInfo>> info,
                                 StatusCallback callback);

  Status GetColocatedTabletSchemaByParentTableId(
      const TableId& parent_colocated_table_id,
      std::shared_ptr<std::vector<YBTableInfo>> info,
      StatusCallback callback);

  Result<IndexPermissions> GetIndexPermissions(
      const TableId& table_id,
      const TableId& index_id);
  Result<IndexPermissions> GetIndexPermissions(
      const YBTableName& table_name,
      const YBTableName& index_name);
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      const TableId& table_id,
      const TableId& index_id,
      const IndexPermissions& target_index_permissions,
      const CoarseTimePoint deadline,
      const CoarseDuration max_wait = std::chrono::seconds(2));
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      const TableId& table_id,
      const TableId& index_id,
      const IndexPermissions& target_index_permissions,
      const CoarseDuration max_wait = std::chrono::seconds(2));
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      const YBTableName& table_name,
      const YBTableName& index_name,
      const IndexPermissions& target_index_permissions,
      const CoarseDuration max_wait = std::chrono::seconds(2));
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      const YBTableName& table_name,
      const YBTableName& index_name,
      const IndexPermissions& target_index_permissions,
      const CoarseTimePoint deadline,
      const CoarseDuration max_wait = std::chrono::seconds(2));

  // Namespace related methods.

  // Create a new namespace with the given name.
  // TODO(neil) When database_type is undefined, backend will not check error on database type.
  // Except for testing we should use proper database_types for all creations.
  Status CreateNamespace(const std::string& namespace_name,
                         const boost::optional<YQLDatabase>& database_type = boost::none,
                         const std::string& creator_role_name = "",
                         const std::string& namespace_id = "",
                         const std::string& source_namespace_id = "",
                         const boost::optional<uint32_t>& next_pg_oid = boost::none,
                         const TransactionMetadata* txn = nullptr,
                         const bool colocated = false,
                         CoarseTimePoint deadline = CoarseTimePoint());

  // It calls CreateNamespace(), but before it checks that the namespace has NOT been yet
  // created. So, it prevents error 'namespace already exists'.
  // TODO(neil) When database_type is undefined, backend will not check error on database type.
  // Except for testing we should use proper database_types for all creations.
  Status CreateNamespaceIfNotExists(const std::string& namespace_name,
                                    const boost::optional<YQLDatabase>& database_type =
                                    boost::none,
                                    const std::string& creator_role_name = "",
                                    const std::string& namespace_id = "",
                                    const std::string& source_namespace_id = "",
                                    const boost::optional<uint32_t>& next_pg_oid =
                                    boost::none,
                                    const bool colocated = false);

  // Set 'create_in_progress' to true if a CreateNamespace operation is in-progress.
  Status IsCreateNamespaceInProgress(const std::string& namespace_name,
                                     const boost::optional<YQLDatabase>& database_type,
                                     const std::string& namespace_id,
                                     bool *create_in_progress);

  // Delete namespace with the given name.
  Status DeleteNamespace(const std::string& namespace_name,
                         const boost::optional<YQLDatabase>& database_type = boost::none,
                         const std::string& namespace_id = "",
                         CoarseTimePoint deadline = CoarseTimePoint());

  // Set 'delete_in_progress' to true if a DeleteNamespace operation is in-progress.
  Status IsDeleteNamespaceInProgress(const std::string& namespace_name,
                                     const boost::optional<YQLDatabase>& database_type,
                                     const std::string& namespace_id,
                                     bool *delete_in_progress);

  YBNamespaceAlterer* NewNamespaceAlterer(const std::string& namespace_name,
                                          const std::string& namespace_id);

  // For Postgres: reserve oids for a Postgres database.
  Status ReservePgsqlOids(const std::string& namespace_id,
                          uint32_t next_oid, uint32_t count,
                          uint32_t* begin_oid, uint32_t* end_oid);

  Status GetYsqlCatalogMasterVersion(uint64_t *ysql_catalog_version);

  // Grant permission with given arguments.
  Status GrantRevokePermission(GrantRevokeStatementType statement_type,
                               const PermissionType& permission,
                               const ResourceType& resource_type,
                               const std::string& canonical_resource,
                               const char* resource_name,
                               const char* namespace_name,
                               const std::string& role_name);

  // List all namespace identifiers.
  Result<std::vector<master::NamespaceIdentifierPB>> ListNamespaces();
  Result<std::vector<master::NamespaceIdentifierPB>> ListNamespaces(
      const boost::optional<YQLDatabase>& database_type);

  // Get namespace information.
  Status GetNamespaceInfo(const std::string& namespace_id,
                          const std::string& namespace_name,
                          const boost::optional<YQLDatabase>& database_type,
                          master::GetNamespaceInfoResponsePB* ret);

  // Check if the namespace given by 'namespace_name' or 'namespace_id' exists.
  // Result value is set only on success.
  Result<bool> NamespaceExists(const std::string& namespace_name,
                               const boost::optional<YQLDatabase>& database_type = boost::none);
  Result<bool> NamespaceIdExists(const std::string& namespace_id,
                                 const boost::optional<YQLDatabase>& database_type = boost::none);

  Status CreateTablegroup(const std::string& namespace_name,
                          const std::string& namespace_id,
                          const std::string& tablegroup_id,
                          const std::string& tablespace_id,
                          const TransactionMetadata* txn);

  Status DeleteTablegroup(const std::string& tablegroup_id);

  // Check if the tablegroup given by 'tablegroup_id' exists.
  // Result value is set only on success.
  Result<bool> TablegroupExists(const std::string& namespace_name,
                                const std::string& tablegroup_id);
  Result<std::vector<master::TablegroupIdentifierPB>> ListTablegroups(
      const std::string& namespace_name);

  // Authentication and Authorization
  // Create a new role.
  Status CreateRole(const RoleName& role_name,
                    const std::string& salted_hash,
                    const bool login, const bool superuser,
                    const RoleName& creator_role_name);

  // Alter an existing role.
  Status AlterRole(const RoleName& role_name,
                   const boost::optional<std::string>& salted_hash,
                   const boost::optional<bool> login,
                   const boost::optional<bool> superuser,
                   const RoleName& current_role_name);

  // Delete a role.
  Status DeleteRole(const std::string& role_name, const std::string& current_role_name);

  Status SetRedisPasswords(const std::vector<std::string>& passwords);
  // Fetches the password from the local cache, or from the master if the local cached value
  // is too old.
  Status GetRedisPasswords(std::vector<std::string>* passwords);

  Status SetRedisConfig(const std::string& key, const std::vector<std::string>& values);
  Status GetRedisConfig(const std::string& key, std::vector<std::string>* values);

  // Grants a role to another role, or revokes a role from another role.
  Status GrantRevokeRole(GrantRevokeStatementType statement_type,
                         const std::string& granted_role_name,
                         const std::string& recipient_role_name);

  // Get all the roles' permissions from the master only if the master's permissions version is
  // greater than permissions_cache->version().s
  Status GetPermissions(client::internal::PermissionsCache* permissions_cache);

  // (User-defined) type related methods.

  // Create a new (user-defined) type.
  Status CreateUDType(const std::string &namespace_name,
                      const std::string &type_name,
                      const std::vector<std::string> &field_names,
                      const std::vector<std::shared_ptr<QLType>> &field_types);

  // Delete a (user-defined) type by name.
  Status DeleteUDType(const std::string &namespace_name, const std::string &type_name);

  // Retrieve a (user-defined) type by name.
  Result<std::shared_ptr<QLType>> GetUDType(
        const std::string &namespace_name, const std::string &type_name);

  // CDC Stream related methods.

  // Create a new CDC stream.
  Result<CDCStreamId> CreateCDCStream(
      const TableId& table_id,
      const std::unordered_map<std::string, std::string>& options,
      bool active = true,
      const NamespaceId& namespace_id = "");

  void CreateCDCStream(const TableId& table_id,
                       const std::unordered_map<std::string, std::string>& options,
                       CreateCDCStreamCallback callback);

  // Delete multiple CDC streams.
  Status DeleteCDCStream(const std::vector<CDCStreamId>& streams,
                         bool force_delete = false,
                         bool ignore_errors = false,
                         master::DeleteCDCStreamResponsePB* resp = nullptr);

  // Delete a CDC stream.
  Status DeleteCDCStream(const CDCStreamId& stream_id,
                         bool force_delete = false,
                         bool ignore_errors = false);

  void DeleteCDCStream(const CDCStreamId& stream_id, StatusCallback callback);

  // Create a new CDC stream.
  Status GetCDCDBStreamInfo(
      const std::string& db_stream_id,
      std::vector<std::pair<std::string, std::string>>* db_stream_info);

  void GetCDCDBStreamInfo(
      const std::string& db_stream_id,
      const std::shared_ptr<std::vector<std::pair<std::string, std::string>>>& db_stream_info,
      const StdStatusCallback& callback);

  // Retrieve a CDC stream.
  Status GetCDCStream(const CDCStreamId &stream_id,
                      NamespaceId* ns_id,
                      std::vector<TableId>* table_ids,
                      std::unordered_map<std::string, std::string>* options);

  void GetCDCStream(const CDCStreamId& stream_id,
                    std::shared_ptr<TableId> table_id,
                    std::shared_ptr<std::unordered_map<std::string, std::string>> options,
                    StdStatusCallback callback);

  void DeleteNotServingTablet(const TabletId& tablet_id, StdStatusCallback callback);

  // Update a CDC stream's options.
  Status UpdateCDCStream(const std::vector<CDCStreamId>& stream_ids,
                         const std::vector<master::SysCDCStreamEntryPB>& new_entries);

  Result<bool> IsBootstrapRequired(const std::vector<TableId>& table_ids,
                                   const boost::optional<CDCStreamId>& stream_id = boost::none);

  // Update consumer pollers after a producer side tablet split.
  Status UpdateConsumerOnProducerSplit(const std::string& producer_id,
                                       const TableId& table_id,
                                       const master::ProducerSplitTabletInfoPB& split_info);

  // Update after a producer DDL change. Returns if caller should wait for a similar Consumer DDL.
  Status UpdateConsumerOnProducerMetadata(const std::string& producer_id,
                                          const TableId& table_id,
                                          const tablet::ChangeMetadataRequestPB& meta_info,
                                          master::UpdateConsumerOnProducerMetadataResponsePB *resp);

  void GetTableLocations(
      const TableId& table_id, int32_t max_tablets, RequireTabletsRunning require_tablets_running,
      PartitionsOnly partitions_only, GetTableLocationsCallback callback);

  // Find the number of tservers. This function should not be called frequently for reading or
  // writing actual data. Currently, it is called only for SQL DDL statements.
  // If primary_only is set to true, we expect the primary/sync cluster tserver count only.
  // If use_cache is set to true, we return old value.
  Status TabletServerCount(int *tserver_count, bool primary_only = false,
      bool use_cache = false);

  Result<std::vector<YBTabletServer>> ListTabletServers();

  Result<TabletServersInfo> ListLiveTabletServers(bool primary_only = false);

  // Sets local tserver and its proxy.
  void SetLocalTabletServer(const std::string& ts_uuid,
                            const std::shared_ptr<tserver::TabletServerServiceProxy>& proxy,
                            const tserver::LocalTabletServer* local_tserver);

  // List only those tables whose names pass a substring match on 'filter'.
  //
  // 'tables' is appended to only on success.
  Result<std::vector<YBTableName>> ListTables(
      const std::string& filter = "",
      bool exclude_ysql = false);

  // List tables in a namespace.
  //
  // 'tables' is appended to only on success.
  Result<std::vector<YBTableName>> ListUserTables(
      const master::NamespaceIdentifierPB& ns_identifier,
      bool include_indexes = false);

  Result<cdc::EnumOidLabelMap> GetPgEnumOidLabelMap(const NamespaceName& ns_name);

  Result<cdc::CompositeAttsMap> GetPgCompositeAttsMap(const NamespaceName& ns_name);

  Result<std::pair<Schema, uint32_t>> GetTableSchemaFromSysCatalog(
      const TableId& table_id, const uint64_t read_time);

  // List all running tablets' uuids for this table.
  // 'tablets' is appended to only on success.
  Status GetTablets(
      const YBTableName& table_name,
      const int32_t max_tablets,
      std::vector<TabletId>* tablet_uuids,
      std::vector<std::string>* ranges,
      std::vector<master::TabletLocationsPB>* locations = nullptr,
      RequireTabletsRunning require_tablets_running = RequireTabletsRunning::kFalse,
      master::IncludeInactive include_inactive = master::IncludeInactive::kFalse);

  Status GetTabletsAndUpdateCache(
      const YBTableName& table_name,
      const int32_t max_tablets,
      std::vector<TabletId>* tablet_uuids,
      std::vector<std::string>* ranges,
      std::vector<master::TabletLocationsPB>* locations);

  Status GetTabletsFromTableId(
      const std::string& table_id, const int32_t max_tablets,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>* tablets);

  // partition_list_version is an output-only parameter.
  Status GetTablets(
      const YBTableName& table_name,
      const int32_t max_tablets,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>* tablets,
      PartitionListVersion* partition_list_version,
      RequireTabletsRunning require_tablets_running = RequireTabletsRunning::kFalse,
      master::IncludeInactive include_inactive = master::IncludeInactive::kFalse);

  Status GetTabletLocation(const TabletId& tablet_id,
                           master::TabletLocationsPB* tablet_location);

  // Get a list of global transaction status tablets, and local transaction status tablets
  // that are local to 'placement'.
  Result<TransactionStatusTablets> GetTransactionStatusTablets(const CloudInfoPB& placement);

  // Get the list of master uuids. Can be enhanced later to also return port/host info.
  Status ListMasters(
    CoarseTimePoint deadline,
    std::vector<std::string>* master_uuids);

  // Check if the table given by 'table_name' exists.
  // Result value is set only on success.
  Result<bool> TableExists(const YBTableName& table_name);

  Result<bool> IsLoadBalanced(uint32_t num_servers);
  Result<bool> IsLoadBalancerIdle();

  Status ModifyTablePlacementInfo(
      const YBTableName& table_name,
      master::PlacementInfoPB* replicas);

  // Creates a transaction status table. 'table_name' is required to start with
  // kTransactionTablePrefix.
  Status CreateTransactionsStatusTable(
      const std::string& table_name,
      const master::ReplicationInfoPB* replication_info = nullptr);

  // Add a tablet to a transaction table.
  Status AddTransactionStatusTablet(const TableId& table_id);

  // Open the table with the given name or id. This will do an RPC to ensure that
  // the table exists and look up its schema.
  // Version with table_id is preferable due to parallel run of RPCs.
  // TODO: should we offer an async version of this as well?
  // TODO: probably should have a configurable timeout in YBClientBuilder?
  Status OpenTable(const YBTableName& table_name, YBTablePtr* table);
  Status OpenTable(const TableId& table_id, YBTablePtr* table,
                   master::GetTableSchemaResponsePB* resp = nullptr);

  Result<YBTablePtr> OpenTable(const TableId& table_id);
  Result<YBTablePtr> OpenTable(const YBTableName& name);

  // Create a new session for interacting with the cluster.
  // User is responsible for destroying the session object.
  // This is a fully local operation (no RPCs or blocking).
  std::shared_ptr<YBSession> NewSession();

  // Return the socket address of the master leader for this client.
  HostPort GetMasterLeaderAddress();

  // Caller knows that the existing leader might have died or stepped down, so it can use this API
  // to reset the client state to point to new master leader.
  Result<HostPort> RefreshMasterLeaderAddress();

  // Refreshes master leader address asynchronously.
  void RefreshMasterLeaderAddressAsync();

  // Once a config change is completed to add/remove a master, update the client to add/remove it
  // from its own master address list.
  Status AddMasterToClient(const HostPort& add);
  Status RemoveMasterFromClient(const HostPort& remove);
  Status SetMasterAddresses(const std::string& addrs);

  // Policy with which to choose amongst multiple replicas.
  enum ReplicaSelection {
    // Select the LEADER replica.
    LEADER_ONLY,

    // Select the closest replica to the client, or a random one if all
    // replicas are equidistant.
    CLOSEST_REPLICA,

    // Select the first replica in the list.
    FIRST_REPLICA
  };

  bool IsMultiMaster() const;

  // Get the number of tablets to be created for a new user table.
  // This will be based on --num_shards_per_tserver or --ysql_num_shards_per_tserver
  // and number of tservers.
  Result<int> NumTabletsForUserTable(TableType table_type);

  void TEST_set_admin_operation_timeout(const MonoDelta& timeout);

  const MonoDelta& default_admin_operation_timeout() const;
  const MonoDelta& default_rpc_timeout() const;

  // Value for the latest observed hybrid_time when none has been observed or set.
  static const uint64_t kNoHybridTime;

  // Returns highest hybrid_time observed by the client.
  // The latest observed hybrid_time can be used to start a snapshot scan on a
  // table which is guaranteed to contain all data written or previously read by
  // this client.
  uint64_t GetLatestObservedHybridTime() const;

  // Sets the latest observed hybrid_time, encoded in the HybridTime format.
  // This is only useful when forwarding hybrid_times between clients to enforce
  // external consistency when using YBSession::CLIENT_PROPAGATED external consistency
  // mode.
  // To use this the user must obtain the HybridTime encoded hybrid_time from the first
  // client with YBClient::GetLatestObservedHybridTime() and the set it in the new
  // client with this method.
  void SetLatestObservedHybridTime(uint64_t ht_hybrid_time);

  // Given a host and port for a master, get the uuid of that process.
  Status GetMasterUUID(const std::string& host, uint16_t port, std::string* uuid);

  Status SetReplicationInfo(const master::ReplicationInfoPB& replication_info);

  // Check if placement information is satisfiable.
  Status ValidateReplicationInfo(const master::ReplicationInfoPB& replication_info);

  // Get the disk size of a table (calculated as SST file size + WAL file size)
  Result<TableSizeInfo> GetTableDiskSize(const TableId& table_id);

  // Provide the completion status of 'txn' to the YB-Master.
  Status ReportYsqlDdlTxnStatus(const TransactionMetadata& txn, bool is_committed);

  Result<bool> CheckIfPitrActive();

  void LookupTabletByKey(const std::shared_ptr<YBTable>& table,
                         const std::string& partition_key,
                         CoarseTimePoint deadline,
                         LookupTabletCallback callback);

  void LookupTabletById(const std::string& tablet_id,
                        const std::shared_ptr<const YBTable>& table,
                        master::IncludeInactive include_inactive,
                        master::IncludeDeleted include_deleted,
                        CoarseTimePoint deadline,
                        LookupTabletCallback callback,
                        UseCache use_cache);

  void LookupAllTablets(const std::shared_ptr<YBTable>& table,
                        CoarseTimePoint deadline,
                        LookupTabletRangeCallback callback);

  // Get the AutoFlagConfig from master. Returns std::nullopt if master is runnning on an older
  // version that does not support AutoFlags.
  Result<std::optional<AutoFlagsConfigPB>> GetAutoFlagConfig();

  std::future<Result<internal::RemoteTabletPtr>> LookupTabletByKeyFuture(
      const std::shared_ptr<YBTable>& table,
      const std::string& partition_key,
      CoarseTimePoint deadline);

  std::future<Result<std::vector<internal::RemoteTabletPtr>>> LookupAllTabletsFuture(
      const std::shared_ptr<YBTable>& table,
      CoarseTimePoint deadline);

  rpc::Messenger* messenger() const;

  const scoped_refptr<MetricEntity>& metric_entity() const;

  rpc::ProxyCache& proxy_cache() const;

  const std::string& proxy_uuid() const;

  // Id of this client instance.
  const ClientId& id() const;

  const CloudInfoPB& cloud_info() const;

  std::pair<RetryableRequestId, RetryableRequestId> NextRequestIdAndMinRunningRequestId();

  void RequestsFinished(const std::set<RetryableRequestId>& request_ids);

  void Shutdown();

  const std::string& LogPrefix() const;

 private:
  class Data;

  friend class YBClientBuilder;
  friend class YBNoOp;
  friend class YBTable;
  friend class YBTableAlterer;
  friend class YBNamespaceAlterer;
  friend class YBTableCreator;
  friend class internal::Batcher;
  friend class internal::GetTableSchemaRpc;
  friend class internal::GetTablegroupSchemaRpc;
  friend class internal::GetColocatedTabletSchemaRpc;
  friend class internal::LookupRpc;
  friend class internal::MetaCache;
  friend class internal::RemoteTablet;
  friend class internal::RemoteTabletServer;
  friend class internal::AsyncRpc;
  friend class internal::TabletInvoker;
  friend class internal::ClientMasterRpcBase;
  friend class PlacementInfoTest;

  FRIEND_TEST(ClientTest, TestGetTabletServerBlacklist);
  FRIEND_TEST(ClientTest, TestMasterDown);
  FRIEND_TEST(ClientTest, TestMasterLookupPermits);
  FRIEND_TEST(ClientTest, TestReplicatedTabletWritesAndAltersWithLeaderElection);
  FRIEND_TEST(ClientTest, TestScanFaultTolerance);
  FRIEND_TEST(ClientTest, TestScanTimeout);
  FRIEND_TEST(ClientTest, TestWriteWithDeadMaster);
  FRIEND_TEST(MasterFailoverTest, DISABLED_TestPauseAfterCreateTableIssued);
  FRIEND_TEST(MasterFailoverTest, TestFailoverAfterNamespaceCreated);
  FRIEND_TEST(MasterFailoverTestIndexCreation, TestPauseAfterCreateIndexIssued);

  friend std::future<Result<internal::RemoteTabletPtr>> LookupFirstTabletFuture(
      YBClient* client, const YBTablePtr& table);

  CoarseTimePoint PatchAdminDeadline(CoarseTimePoint deadline) const;

  YBClient();

  ThreadPool* callback_threadpool();

  std::unique_ptr<Data> data_;

  DISALLOW_COPY_AND_ASSIGN(YBClient);
};

Result<TableId> GetTableId(YBClient* client, const YBTableName& table_name);

}  // namespace client
}  // namespace yb
