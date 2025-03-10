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

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/consensus/opid_util.h"

#include "yb/cdc/cdc_service.pb.h"

#include "yb/util/status.h"
#include "yb/util/status_callback.h"

namespace yb {

namespace client {

class YBTableName;

} // namespace client

namespace cdc {

struct OutputClientResponse {
  Status status;
  OpIdPB last_applied_op_id;
  uint32_t processed_record_count;
  uint32_t wait_for_version { 0 };
};

class CDCOutputClient : public std::enable_shared_from_this<CDCOutputClient> {
 public:
  virtual ~CDCOutputClient() {}
  virtual void Shutdown() {}

  // Sets the last compatible consumer schema version
  virtual void SetLastCompatibleConsumerSchemaVersion(SchemaVersion schema_version) = 0;

  // Async call for applying changes.
  virtual Status ApplyChanges(const cdc::GetChangesResponsePB* resp) = 0;
};

} // namespace cdc
} // namespace yb
