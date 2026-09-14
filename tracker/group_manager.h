#ifndef GROUP_MANAGER_H
#define GROUP_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

struct Group {
    std::string group_id;
    std::string owner_id;
    std::unordered_set<std::string> members;
    std::unordered_set<std::string> pending_requests;
};

class GroupManager {
public:
    GroupManager() = default;

    // Create group with creator as owner and initial member
    bool create_group(const std::string &group_id, const std::string &owner_id, std::string &err_msg);

    // Submit a join request for a group
    bool join_group(const std::string &group_id, const std::string &user_id, std::string &err_msg);

    // Leave a group
    bool leave_group(const std::string &group_id, const std::string &user_id, std::string &err_msg);

    // List all groups in the system
    std::vector<std::string> list_groups();

    // List pending join requests for a group (owner only)
    bool list_requests(const std::string &group_id, const std::string &caller_id, std::vector<std::string> &out_requests, std::string &err_msg);

    // Accept a pending join request (owner only)
    bool accept_request(const std::string &group_id, const std::string &target_user_id, const std::string &caller_id, std::string &err_msg);

    // Membership check
    bool is_member(const std::string &group_id, const std::string &user_id);

    // Owner check
    bool is_owner(const std::string &group_id, const std::string &user_id);

    // Existence check
    bool group_exists(const std::string &group_id);

    // Synchronized mutators from peer tracker
    void apply_sync_create_group(const std::string &group_id, const std::string &owner_id);
    void apply_sync_join_group(const std::string &group_id, const std::string &user_id);
    void apply_sync_accept_request(const std::string &group_id, const std::string &user_id);
    void apply_sync_leave_group(const std::string &group_id, const std::string &user_id);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Group> groups_; // group_id -> Group
};

#endif // GROUP_MANAGER_H
