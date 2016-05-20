// Copyright (c) 2016, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "agent_impl.h"
#include "rpc/rpc_client.h"
#include "boost/thread/mutex.hpp"
#include "protocol/resman.pb.h"

#include <string>
#include <sstream>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
//#include <sys/utsname.h>
#include <gflags/gflags.h>
#include <boost/bind.hpp>
#include <snappy.h>

DECLARE_string(agent_ip);
DECLARE_string(agent_port);
DECLARE_int32(keepalive_interval);

namespace baidu {
namespace galaxy {

AgentImpl::AgentImpl() :
    heartbeat_pool_(1),
    master_rpc_(new baidu::galaxy::RpcClient()),
    rm_watcher_(new baidu::galaxy::MasterWatcher()),
    resman_stub_(NULL),
    agent_endpoint_(FLAGS_agent_ip + ":" + FLAGS_agent_port),
    running_(false),
    rm_(new baidu::galaxy::resource::ResourceManager),
    cm_(new baidu::galaxy::container::ContainerManager(rm_)),
    health_checker_(new baidu::galaxy::health::HealthChecker()),
    start_time_(baidu::common::timer::get_micros())
{
    version_ = "0.0.1";
    //version_ = __DATE__ + __TIME__;
}

AgentImpl::~AgentImpl()
{
    running_ = false;
}

void AgentImpl::HandleMasterChange(const std::string& new_master_endpoint)
{
    if (new_master_endpoint.empty()) {
        LOG(WARNING) << "endpoint of RM is deleted from nexus";
    }
    if (new_master_endpoint != master_endpoint_) {
        boost::mutex::scoped_lock lock(rpc_mutex_);
        LOG(INFO) << "RM changes to " << new_master_endpoint.c_str();
        master_endpoint_ = new_master_endpoint;

        if (resman_stub_) {
            delete resman_stub_;
            resman_stub_ = NULL;
        }

        if (!master_rpc_->GetStub(master_endpoint_, &resman_stub_)) {
            LOG(WARNING) << "connect RM failed: " << master_endpoint_.c_str();
            return;
        }
    }
}

void AgentImpl::Setup()
{
    running_ = true;
    if (0 != rm_->Load()) {
        LOG(FATAL) << "resource manager loads resource failed";
        exit(1);
    }
    LOG(INFO) << "resource manager load resource successfully: " << rm_->ToString();

    if (!rm_watcher_->Init(boost::bind(&AgentImpl::HandleMasterChange, this, _1))) {
        LOG(FATAL) << "init res manager watch failed, agent will exit ...";
        exit(1);
    }
    LOG(INFO) << "init resource manager watcher successfully";


    heartbeat_pool_.AddTask(boost::bind(&AgentImpl::KeepAlive, this, FLAGS_keepalive_interval));
    LOG(INFO) << "start keep alive thread, interval is " << FLAGS_keepalive_interval << "ms";
}

void AgentImpl::KeepAlive(int internal_ms)
{
    baidu::galaxy::proto::KeepAliveRequest request;
    baidu::galaxy::proto::KeepAliveResponse response;
    request.set_endpoint(agent_endpoint_);

    boost::mutex::scoped_lock lock(rpc_mutex_);
    if (!master_rpc_->SendRequest(resman_stub_,
            &galaxy::proto::ResMan_Stub::KeepAlive,
            &request,
            &response,
            5,
            1)) {
        LOG(WARNING) << "keep alive failed";
    }

    heartbeat_pool_.DelayTask(internal_ms, boost::bind(&AgentImpl::KeepAlive, this, internal_ms));
}

void AgentImpl::CreateContainer(::google::protobuf::RpcController* controller,
        const ::baidu::galaxy::proto::CreateContainerRequest* request,
        ::baidu::galaxy::proto::CreateContainerResponse* response,
        ::google::protobuf::Closure* done)
{
    baidu::galaxy::container::ContainerId id(request->container_group_id(), request->id());
    baidu::galaxy::proto::ErrorCode* ec = new baidu::galaxy::proto::ErrorCode();

    if (0 != cm_->CreateContainer(id, request->container())) {
        ec->set_status(baidu::galaxy::proto::kError);
        ec->set_reason("failed");
    } else {
        ec->set_status(baidu::galaxy::proto::kOk);
        ec->set_reason("sucess");
    }

    response->set_allocated_code(ec);
    done->Run();
}

void AgentImpl::RemoveContainer(::google::protobuf::RpcController* controller,
        const ::baidu::galaxy::proto::RemoveContainerRequest* request,
        ::baidu::galaxy::proto::RemoveContainerResponse* response,
        ::google::protobuf::Closure* done)
{
}

void AgentImpl::ListContainers(::google::protobuf::RpcController* controller,
        const ::baidu::galaxy::proto::ListContainersRequest* request,
        ::baidu::galaxy::proto::ListContainersResponse* response,
        ::google::protobuf::Closure* done)
{
}

void AgentImpl::Query(::google::protobuf::RpcController* controller,
        const ::baidu::galaxy::proto::QueryRequest* request,
        ::baidu::galaxy::proto::QueryResponse* response,
        ::google::protobuf::Closure* done)
{

    baidu::galaxy::proto::AgentInfo* ai = new baidu::galaxy::proto::AgentInfo();
    ai->set_unhealthy(!health_checker_->Healthy());
    ai->set_start_time(start_time_);
    ai->set_version(version_);

    baidu::galaxy::proto::Resource* cpu_resource = new baidu::galaxy::proto::Resource();
    cpu_resource->CopyFrom(*(rm_->GetCpuResource()));
    //cpu_resource->set_used(0);
    ai->set_allocated_cpu_resource(cpu_resource);

    baidu::galaxy::proto::Resource* memory_resource = new baidu::galaxy::proto::Resource();
    memory_resource->CopyFrom(*(rm_->GetMemoryResource()));
    //memory_resource->set_used(0);

    std::vector<boost::shared_ptr<baidu::galaxy::proto::VolumResource> > vrs;
    rm_->GetVolumResource(vrs);

    for (size_t i = 0; i < vrs.size(); i++) {
        baidu::galaxy::proto::VolumResource* vr = ai->add_volum_resources();
        vr->set_device_path(vrs[i]->device_path());
        vr->set_medium(vrs[i]->medium());
        baidu::galaxy::proto::Resource* r = new baidu::galaxy::proto::Resource();
        r->set_total(vrs[i]->volum().total());
        r->set_assigned(vrs[i]->volum().assigned());
        //r->set_used();
        vr->set_allocated_volum(r);
    }

    if (request->has_full_report() && request->full_report()) {

    }

    baidu::galaxy::proto::ErrorCode* ec = new baidu::galaxy::proto::ErrorCode();
}


}
}
