// Copyright (c) 2016, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include "protocol/galaxy.pb.h"
#include "container_status.h"

#include <sys/types.h>

#include <boost/shared_ptr.hpp>
#include <google/protobuf/message.h>

#include <string>
#include <vector>
#include <map>

namespace baidu {
namespace galaxy {
namespace cgroup {
class Cgroup;
}

namespace collector {
class Collector;
}

namespace volum {
class VolumGroup;
}

namespace container {
class Process;

class ContainerId {
public:
    ContainerId() {}
    ContainerId(const std::string& group_id, const std::string& container_id) :
        group_id_(group_id),
        container_id_(container_id) {
    }

    bool Empty() {
        return (group_id_.empty() || container_id_.empty());
    }

    ContainerId& SetGroupId(const std::string& id) {
        group_id_ = id;
        return *this;
    }

    ContainerId& SetSubId(const std::string& id) {
        container_id_ = id;
        return *this;
    }

    const std::string& GroupId() const {
        return group_id_;
    }

    const std::string& SubId() const {
        return container_id_;
    }

    const std::string CompactId() const {
        return group_id_ + "_" + container_id_;
    }

    const std::string ToString() const {
        std::string ret;
        ret += "group_id: ";
        ret += group_id_;
        ret += " ";
        ret += "container_id: ";
        ret += container_id_;
        return ret;
    }

    friend bool operator < (const ContainerId& l, const ContainerId& r) {
        if (l.group_id_ == r.GroupId()) {
            return (l.container_id_ < r.container_id_);
        } else {
            return l.group_id_ < r.group_id_;
        }
    }

    friend bool operator == (const ContainerId& l, const ContainerId& r) {
        return (l.group_id_ == r.group_id_ && l.container_id_ == r.container_id_);
    }
private:
    std::string group_id_;
    std::string container_id_;

};

class Container {
public:
    Container(const ContainerId& id, const baidu::galaxy::proto::ContainerDescription& desc);
    ~Container();

    void AddEnv(const std::string& key, const std::string& value);
    void AddEnv(const std::map<std::string, std::string>& env);

    ContainerId Id() const;
    int Construct();
    int Destroy();

    int Tasks(std::vector<pid_t>& pids);
    int Pids(std::vector<pid_t>& pids);
    boost::shared_ptr<google::protobuf::Message> Report();
    baidu::galaxy::proto::ContainerStatus Status();
    const baidu::galaxy::proto::ContainerDescription& Description();

    boost::shared_ptr<baidu::galaxy::proto::ContainerInfo> ContainerInfo(bool full_info);
private:
    int Construct_();
    int Destroy_();

    int RunRoutine(void*);
    void ExportEnv(std::map<std::string, std::string>& env);
    void ExportEnv();
    bool Alive();

    const baidu::galaxy::proto::ContainerDescription desc_;
    std::vector<boost::shared_ptr<baidu::galaxy::cgroup::Cgroup> > cgroup_;
    boost::shared_ptr<baidu::galaxy::volum::VolumGroup> volum_group_;
    boost::shared_ptr<Process> process_;
    ContainerId id_;
    baidu::galaxy::container::ContainerStatus status_;
    std::vector<boost::shared_ptr<baidu::galaxy::collector::Collector> > collectors_;

};

} //namespace container
} //namespace galaxy
} //namespace baidu
