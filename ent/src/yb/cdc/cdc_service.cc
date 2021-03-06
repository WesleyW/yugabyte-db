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

#include "yb/cdc/cdc_service.h"

#include <memory>

#include "yb/cdc/cdc_producer.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/service_util.h"
#include "yb/util/debug/trace_event.h"

namespace yb {
namespace cdc {

using consensus::LeaderStatus;
using consensus::RaftConsensus;
using rpc::RpcContext;
using tserver::TSTabletManager;

CDCServiceImpl::CDCServiceImpl(TSTabletManager* tablet_manager,
                               const scoped_refptr<MetricEntity>& metric_entity)
    : CDCServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

void CDCServiceImpl::SetupCDC(const SetupCDCRequestPB* req,
                              SetupCDCResponsePB* resp,
                              RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

void CDCServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                 ListTabletsResponsePB* resp,
                                 RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

void CDCServiceImpl::GetChanges(const GetChangesRequestPB* req,
                                GetChangesResponsePB* resp,
                                RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }
  const auto& tablet_peer = GetLeaderTabletPeer(req->tablet_id(), resp, &context);
  if (!tablet_peer.ok()) {
    return;
  }

  CDCProducer cdc_producer(*tablet_peer);
  Status status = cdc_producer.GetChanges(*req, resp);
  if (PREDICT_FALSE(!status.ok())) {
    // TODO: Map other error statuses to CDCErrorPB.
    SetupErrorAndRespond(
        resp->mutable_error(),
        status,
        status.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
        &context);
    return;
  }

  context.RespondSuccess();
}

void CDCServiceImpl::GetCheckpoint(const GetCheckpointRequestPB* req,
                                   GetCheckpointResponsePB* resp,
                                   RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

template <class ReqType, class RespType>
bool CDCServiceImpl::CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc) {
  TRACE("Received RPC $0: $1", rpc->ToString(), req->DebugString());
  if (PREDICT_FALSE(!tablet_manager_)) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Server is not running"),
                         CDCErrorPB::NOT_RUNNING,
                         rpc);
    return false;
  }
  return true;
}

template <class RespType>
Result<std::shared_ptr<tablet::TabletPeer>> CDCServiceImpl::GetLeaderTabletPeer(
    const std::string& tablet_id,
    RespType* resp,
    rpc::RpcContext* rpc) {
  std::shared_ptr<tablet::TabletPeer> peer;
  Status status = tablet_manager_->GetTabletPeer(tablet_id, &peer);
  if (PREDICT_FALSE(!status.ok())) {
    CDCErrorPB::Code code = status.IsNotFound() ?
        CDCErrorPB::TABLET_NOT_FOUND : CDCErrorPB::TABLET_NOT_RUNNING;
    SetupErrorAndRespond(resp->mutable_error(), status, code, rpc);
    return status;
  }

  // Check RUNNING state.
  status = peer->CheckRunning();
  if (PREDICT_FALSE(!status.ok())) {
    Status s = STATUS(IllegalState, "Tablet not RUNNING");
    SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::TABLET_NOT_RUNNING, rpc);
    return s;
  }

  // Check if tablet peer is leader.
  consensus::LeaderStatus leader_status = peer->LeaderStatus();
  if (leader_status != consensus::LeaderStatus::LEADER_AND_READY) {
    // No records to read.
    if (leader_status == consensus::LeaderStatus::NOT_LEADER) {
      // TODO: Change this to provide new leader
    }
    Status s = STATUS(IllegalState, "Tablet Server is not leader", ToCString(leader_status));
    SetupErrorAndRespond(
        resp->mutable_error(),
        s,
        CDCErrorPB::NOT_LEADER,
        rpc);
    return s;
  }
  return peer;
}

}  // namespace cdc
}  // namespace yb
