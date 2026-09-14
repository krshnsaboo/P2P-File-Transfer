#include "group_manager.h"
#include <algorithm>

bool GroupManager::create_group(const std::string &group_id, const std::string &owner_id, std::string &err_msg) {
    if (group_id.empty() || owner_id.empty()) {
        err_msg = "Group ID and owner ID cannot be empty";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (groups_.find(group_id) != groups_.end()) {
        err_msg = "Group '" + group_id + "' already exists";
        return false;
    }

    Group g;
    g.group_id = group_id;
    g.owner_id = owner_id;
    g.members.insert(owner_id);

    groups_[group_id] = g;
    return true;
}

bool GroupManager::join_group(const std::string &group_id, const std::string &user_id, std::string &err_msg) {
    if (group_id.empty() || user_id.empty()) {
        err_msg = "Group ID and user ID cannot be empty";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it == groups_.end()) {
        err_msg = "Group '" + group_id + "' does not exist";
        return false;
    }

    Group &g = it->second;
    if (g.members.find(user_id) != g.members.end()) {
        err_msg = "User is already a member of group '" + group_id + "'";
        return false;
    }

    if (g.pending_requests.find(user_id) != g.pending_requests.end()) {
        err_msg = "Join request already pending for group '" + group_id + "'";
        return false;
    }

    g.pending_requests.insert(user_id);
    return true;
}

bool GroupManager::leave_group(const std::string &group_id, const std::string &user_id, std::string &err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it == groups_.end()) {
        err_msg = "Group '" + group_id + "' does not exist";
        return false;
    }

    Group &g = it->second;
    auto mem_it = g.members.find(user_id);
    if (mem_it == g.members.end()) {
        err_msg = "User is not a member of group '" + group_id + "'";
        return false;
    }

    g.members.erase(mem_it);

    // If owner leaves, reassign ownership or delete group if empty
    if (g.owner_id == user_id) {
        if (g.members.empty()) {
            groups_.erase(it);
            return true;
        } else {
            g.owner_id = *g.members.begin();
        }
    }

    return true;
}

std::vector<std::string> GroupManager::list_groups() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(groups_.size());
    for (const auto &pair : groups_) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool GroupManager::list_requests(const std::string &group_id, const std::string &caller_id, 
                                 std::vector<std::string> &out_requests, std::string &err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it == groups_.end()) {
        err_msg = "Group '" + group_id + "' does not exist";
        return false;
    }

    const Group &g = it->second;
    if (g.owner_id != caller_id) {
        err_msg = "Permission denied: Only group owner ('" + g.owner_id + "') can view join requests";
        return false;
    }

    out_requests.assign(g.pending_requests.begin(), g.pending_requests.end());
    std::sort(out_requests.begin(), out_requests.end());
    return true;
}

bool GroupManager::accept_request(const std::string &group_id, const std::string &target_user_id, 
                                  const std::string &caller_id, std::string &err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it == groups_.end()) {
        err_msg = "Group '" + group_id + "' does not exist";
        return false;
    }

    Group &g = it->second;
    if (g.owner_id != caller_id) {
        err_msg = "Permission denied: Only group owner ('" + g.owner_id + "') can accept requests";
        return false;
    }

    auto req_it = g.pending_requests.find(target_user_id);
    if (req_it == g.pending_requests.end()) {
        err_msg = "No pending request found for user '" + target_user_id + "' in group '" + group_id + "'";
        return false;
    }

    g.pending_requests.erase(req_it);
    g.members.insert(target_user_id);
    return true;
}

bool GroupManager::is_member(const std::string &group_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        return it->second.members.find(user_id) != it->second.members.end();
    }
    return false;
}

bool GroupManager::is_owner(const std::string &group_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        return it->second.owner_id == user_id;
    }
    return false;
}

bool GroupManager::group_exists(const std::string &group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return groups_.find(group_id) != groups_.end();
}

void GroupManager::apply_sync_create_group(const std::string &group_id, const std::string &owner_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (groups_.find(group_id) == groups_.end()) {
        Group g;
        g.group_id = group_id;
        g.owner_id = owner_id;
        g.members.insert(owner_id);
        groups_[group_id] = g;
    }
}

void GroupManager::apply_sync_join_group(const std::string &group_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        if (it->second.members.find(user_id) == it->second.members.end()) {
            it->second.pending_requests.insert(user_id);
        }
    }
}

void GroupManager::apply_sync_accept_request(const std::string &group_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        it->second.pending_requests.erase(user_id);
        it->second.members.insert(user_id);
    }
}

void GroupManager::apply_sync_leave_group(const std::string &group_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        it->second.members.erase(user_id);
        if (it->second.owner_id == user_id) {
            if (it->second.members.empty()) {
                groups_.erase(it);
            } else {
                it->second.owner_id = *it->second.members.begin();
            }
        }
    }
}

