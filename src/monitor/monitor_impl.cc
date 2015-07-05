// Copyright (c) 2015, Galaxy Authors. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: zhoushiyong@baidu.com

#include <fstream>
#include <sstream>
#include <time.h>
#include <vector>
#include <sys/stat.h>
#include <errno.h>
#include <boost/bind.hpp>
#include "common/asm_atomic.h"
#include "common/logging.h"
#include "common/util.h"
#include "proto/monitor.pb.h"
#include "monitor_impl.h"

namespace galaxy {
MonitorImpl::MonitorImpl() {
    running_ = false;
    msg_forbit_time_ = 60;
    thread_pool_.AddTask(boost::bind(&MonitorImpl::Reporting, this));
}
MonitorImpl::~MonitorImpl() {
    std::map<std::string, Watch*>::iterator watch_it;
    for (watch_it == watch_map_.begin(); watch_it != watch_map_.end(); watch_it++) {
        delete watch_it->second;
    }
    
    std::map<std::string, Trigger*>::iterator trigger_it;
    for (trigger_it == trigger_map_.begin(); trigger_it != trigger_map_.end();
            trigger_it++) {
        delete trigger_it->second;
    }

    std::map<std::string, Action*>::iterator action_it;
    for (action_it == action_map_.begin(); action_it != action_map_.end(); 
            action_it++) {
        delete action_it->second;
    }

    std::vector<Rule*>::iterator rule_it;
    for (rule_it == rule_list_.begin(); rule_it != rule_list_.end();
            rule_it++) {
        delete *rule_it;
    }
    return;
}

void MonitorImpl::Split(std::string& src, std::string& delim, std::vector<std::string>* ret) {
    size_t last = 0;  
    size_t index=src.find_first_of(delim, last);  
    while (index!=std::string::npos) {  
        ret->push_back(src.substr(last,index-last));
        last=index+1;  
        index=src.find_first_of(delim,last);  
    }  
    if (index-last>0) {  
        ret->push_back(src.substr(last,index-last));  
    }
    return;
}
bool MonitorImpl::ParseConfig(const std::string conf_path) {
    std::ifstream fin(conf_path.c_str());
    if (!fin) {
        LOG(WARNING, "open conf_path err %s", conf_path.c_str());
        return false;
    }
    std::stringstream ss;
    {
        char * buffer = new char[1024];
        while (!fin.eof()) {
            fin.read(buffer, 1024);
            ss.write(buffer, fin.gcount());
        }
        delete buffer;
    }
    MonitorConfigMsg monitor_config;
    if (!monitor_config.ParseFromString(ss.str())) {
        for (int rule_index = 0; rule_index < monitor_config.rules_size(); rule_index++) {
            const RuleMsg rule = monitor_config.rules(rule_index);
            // config watch
            Watch* watch_ptr = new Watch();
            watch_ptr->item_name.assign(rule.watch().name());
            watch_ptr->regex.assign(rule.watch().regex());
            watch_ptr->count = 0;
            watch_map_[watch_ptr->item_name] = watch_ptr;
            // config trigger
            Trigger* trigger_ptr = new Trigger();
            trigger_ptr->item_name = rule.trigger().name();
            trigger_ptr->threadhold = rule.trigger().threshold();
            trigger_ptr->range = rule.trigger().range();
            trigger_ptr->timestamp = time(NULL);
            trigger_map_[rule.trigger().name()] = trigger_ptr;
            // config action
            Action* action_ptr = new Action();
            action_ptr->title.assign(rule.action().title());
            action_ptr->content.assign(rule.action().content());
            for (int recv_index = 0; recv_index < rule.action().send_list_size(); recv_index++) {
                action_ptr->to_list.push_back(rule.action().send_list(recv_index));
            }
            action_map_[rule.action().title()] = action_ptr;
            // config rule
            Rule* rule_ptr = new Rule();
            rule_ptr->watch = watch_ptr;
            rule_ptr->trigger = trigger_ptr;
            rule_ptr->action = action_ptr;
            rule_list_.push_back(rule_ptr);
        }
        return true;
    }
    return false;
}

void MonitorImpl::Run() {
    size_t seek = 0;
    struct stat* st_mark = new struct stat;
    while (0 != stat(log_path.c_str(), st_mark)) {
        LOG(WARNING, "stat log file err %s [%d:%s]", log_path.c_str(),
                errno, strerror(errno));
        sleep(1);
        continue;
    }
    std::ifstream fin(log_path.c_str());
    fin.seekg(0, std::ios::end);
    seek = fin.tellg();
    fin.seekg(seek, std::ios::beg);
    std::string line;
    running_ = true;

    while (running_) {
        if (fin.peek() == EOF) {  
            struct stat* st_tmp = new struct stat;
            if (0 != stat(log_path.c_str(), st_tmp)) {
                LOG(WARNING, "stat log file err %s [%d:%s]", log_path.c_str(),
                     errno, strerror(errno));
                delete st_tmp;
                sleep(1);
                continue;
            } else if (st_tmp->st_ino != st_mark->st_ino) {
                fin.close();
                fin.clear();
                fin.open(log_path.c_str());
                delete st_mark;
                st_mark = st_tmp;
                continue;
            } else if (seek != 0) {
                delete st_tmp;
                fin.clear();  
                fin.seekg(seek, std::ios::beg);  
                sleep(1);  
                continue;  
            } else {
                delete st_tmp;
                sleep(1);
                continue;
            }
        }
        getline(fin, line);
        seek = fin.tellg();
        ExecRule(std::string(line));
    }
    delete st_mark;
    fin.close();
    return;
}

bool MonitorImpl::ExecRule(std::string src) {
    for (std::vector<Rule*>::iterator it = rule_list_.begin();
            it != rule_list_.end(); it++) {
        if (Matching(src, (*it)->watch)) {
            LOG(INFO, "Matching hit %s", src.c_str());
        }
    }
    return true;
}

bool MonitorImpl::Matching(std::string src, Watch* watch) {
    assert(watch != NULL);
    boost::cmatch mat;
    if (boost::regex_search(src.c_str(), mat, watch->reg)) {
        common::atomic_inc(&watch->count);
        return true;
    }
    return false;
}

bool MonitorImpl::Judging( int* cnt, Trigger* trigger) {
    assert(trigger != NULL);
    bool ret = false;
    if (trigger->relate == "<") {
        ret = (*cnt < trigger->threadhold);
    } else if (trigger->relate == std::string("=")) {
        ret = (*cnt == trigger->threadhold);
    } else if (trigger->relate == std::string(">")) {
        ret = (*cnt > trigger->threadhold);
    } else {
        LOG(WARNING, "unsupported relate %s", trigger->relate.c_str());       
    }

    if (time(NULL) - trigger->timestamp >= trigger->range) {
        common::atomic_swap(cnt, 0);
        trigger->timestamp = time(NULL);
        return ret;
    }
    return false;
}

bool MonitorImpl::Treating(Action* act) {
    assert(act != NULL);
    if (act->timestamp == 0) {
        act->timestamp = time(NULL);
    } else if ((time(NULL) - act->timestamp) < msg_forbit_time_) {
        return true;
    }
    for (std::vector<std::string>::iterator it = act->to_list.begin();
            it != act->to_list.end(); it++) { 
        std::string cmd = std::string("/home/galaxy/tools/gsmsend ")
            + std::string("-s emp01.baidu.com:15001 -semp02.baidu.com:15001 ") 
            + *it + "@" + "\"from:" + common::util::GetLocalHostName() 
            + "\n" + act->title + ":" + act->content + "\"";
        if (0 != system(cmd.c_str())) {
            LOG(WARNING, "gsmsend msg err %s", cmd.c_str());
        }
        act->timestamp = time(NULL);
    }
    return true;
}

void MonitorImpl::Reporting() {
    for (std::vector<Rule*>::iterator it = rule_list_.begin();
            it != rule_list_.end(); it++) {
        if (!Judging(&((*it)->watch->count), (*it)->trigger)) {
            continue;
        }
        if (!Treating((*it)->action)) {
            continue;
        }
    }
    thread_pool_.DelayTask(3000, boost::bind(&MonitorImpl::Reporting, this));
    return;
}
}


/* vim: set ts=4 sw=4 sts=4 tw=100 */
