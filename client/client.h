#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

struct TrackerEndpoint {
    std::string ip;
    int port;
};

class ClientApp {
public:
    ClientApp(const std::string &my_endpoint, const std::string &tracker_info_path);
    ~ClientApp();

    bool init();
    void run();
    void shutdown();

private:
    std::string my_endpoint_raw_;
    std::string my_ip_;
    int my_port_;

    std::string tracker_info_path_;
    std::vector<TrackerEndpoint> trackers_;
    int active_tracker_index_; // 0 or 1
    int tracker_sock_fd_;
    std::string tracker_recv_buffer_;

    std::atomic<bool> running_;
    int peer_server_sock_;
    std::thread peer_listener_thread_;

    // Session state
    std::string logged_in_user_;

    bool parse_config();
    bool connect_to_active_tracker();
    bool ensure_tracker_connection();
    std::string send_tracker_command(const std::string &cmd);

    void start_peer_listener();
    void peer_listener_loop();
    void handle_peer_connection(int peer_fd);

    void handle_local_command(const std::string &cmd_line);
};

#endif // CLIENT_H
