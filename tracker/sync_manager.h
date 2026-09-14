#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

struct SyncEvent {
    uint64_t seq_id = 0;
    std::string command;
};

class UserManager;
class GroupManager;

class SyncManager {
public:
    SyncManager(int my_tracker_no, const std::string &peer_ip, int peer_port,
                UserManager &user_mgr, GroupManager &group_mgr);
    ~SyncManager();

    // Start background sync client thread
    void start();

    // Stop sync threads and close sockets
    void stop();

    // Record a local mutation to WAL and broadcast to peer if online
    void record_and_broadcast(const std::string &mutation_cmd);

    // Process incoming sync message (called either by sync worker or tracker server handler)
    void process_sync_message(const std::string &msg, int source_fd);

    // Check if peer tracker is currently reachable
    bool is_peer_online() const;

    // Get current local sequence number
    uint64_t get_latest_seq() const;

private:
    int my_tracker_no_;
    std::string peer_ip_;
    int peer_port_;

    UserManager &user_mgr_;
    GroupManager &group_mgr_;

    std::atomic<bool> running_;
    std::atomic<bool> peer_online_;
    std::atomic<int> outbound_fd_;

    std::atomic<uint64_t> current_seq_id_;
    std::atomic<uint64_t> last_received_seq_id_;

    mutable std::mutex log_mutex_;
    std::vector<SyncEvent> event_log_;

    mutable std::mutex socket_mutex_;
    std::thread sync_client_thread_;

    void sync_client_loop();
    void send_to_peer(const std::string &payload);
    void apply_mutation(const std::string &mutation_cmd);
    void replay_events_since(uint64_t since_seq, int target_fd);
};

#endif // SYNC_MANAGER_H
