#ifndef TRACKER_H
#define TRACKER_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>

#include "user_manager.h"
#include "group_manager.h"

struct TrackerEndpoint {
    std::string ip;
    int port;
};

class TrackerServer {
public:
    TrackerServer(const std::string &info_filepath, int tracker_no);
    ~TrackerServer();

    // Start server, console loop, and accept clients
    bool init();
    void run();
    void shutdown();

private:
    std::string config_path_;
    int tracker_no_; // 1 or 2
    TrackerEndpoint my_endpoint_;
    TrackerEndpoint peer_endpoint_;

    int server_socket_;
    std::atomic<bool> running_;

    std::thread console_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex client_threads_mutex_;

    UserManager user_mgr_;
    GroupManager group_mgr_;

    bool parse_config();
    void console_loop();
    void handle_client(int client_fd);
    std::string process_command(const std::string &raw_cmd, int client_fd);
};

#endif // TRACKER_H
