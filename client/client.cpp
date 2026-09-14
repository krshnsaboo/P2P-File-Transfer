#include "client.h"
#include "../common/network_utils.h"
#include "../common/protocol.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>

ClientApp::ClientApp(const std::string &my_endpoint, const std::string &tracker_info_path)
    : my_endpoint_raw_(my_endpoint),
      my_ip_("127.0.0.1"),
      my_port_(0),
      tracker_info_path_(tracker_info_path),
      active_tracker_index_(0),
      tracker_sock_fd_(-1),
      running_(false),
      peer_server_sock_(-1) {
}

ClientApp::~ClientApp() {
    shutdown();
}

bool ClientApp::parse_config() {
    if (!NetworkUtils::parse_ip_port(my_endpoint_raw_, my_ip_, my_port_)) {
        std::cerr << "[Client] Error: Invalid peer endpoint '" << my_endpoint_raw_ 
                  << "'. Format should be <IP>:<PORT>" << std::endl;
        return false;
    }

    std::ifstream infile(tracker_info_path_);
    if (!infile.is_open()) {
        std::cerr << "[Client] Error: Cannot open tracker info file at '" << tracker_info_path_ << "'" << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(infile, line)) {
        line = NetworkUtils::trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        std::string ip;
        int port = 0;
        if (NetworkUtils::parse_ip_port(line, ip, port)) {
            trackers_.push_back({ip, port});
        }
    }

    if (trackers_.empty()) {
        std::cerr << "[Client] Error: No valid tracker endpoints found in " << tracker_info_path_ << std::endl;
        return false;
    }

    std::cout << "[Client] Loaded " << trackers_.size() << " tracker endpoint(s)." << std::endl;
    for (size_t i = 0; i < trackers_.size(); ++i) {
        std::cout << "  Tracker " << (i + 1) << ": " << trackers_[i].ip << ":" << trackers_[i].port << std::endl;
    }
    return true;
}

bool ClientApp::connect_to_active_tracker() {
    if (tracker_sock_fd_ >= 0) {
        ::close(tracker_sock_fd_);
        tracker_sock_fd_ = -1;
        tracker_recv_buffer_.clear();
    }

    // Try current active tracker, if it fails, try the alternative
    size_t attempts = trackers_.size();
    for (size_t i = 0; i < attempts; ++i) {
        const auto &target = trackers_[active_tracker_index_];
        int fd = NetworkUtils::connect_to_server(target.ip, target.port, 2);
        if (fd >= 0) {
            tracker_sock_fd_ = fd;
            std::cout << "[Client] Connected to Tracker " << (active_tracker_index_ + 1)
                      << " (" << target.ip << ":" << target.port << ")" << std::endl;
            return true;
        }

        std::cout << "[Client] Tracker " << (active_tracker_index_ + 1)
                  << " (" << target.ip << ":" << target.port << ") unreachable, trying alternate..." << std::endl;
        active_tracker_index_ = (active_tracker_index_ + 1) % trackers_.size();
    }

    std::cerr << "[Client] Warning: Could not connect to any tracker server." << std::endl;
    return false;
}

bool ClientApp::ensure_tracker_connection() {
    if (tracker_sock_fd_ >= 0) {
        return true;
    }
    return connect_to_active_tracker();
}

std::string ClientApp::send_tracker_command(const std::string &cmd) {
    if (!ensure_tracker_connection()) {
        return "ERROR: Unable to connect to tracker";
    }

    std::string payload = cmd + Protocol::DELIMITER;
    if (!NetworkUtils::safe_send(tracker_sock_fd_, payload)) {
        std::cerr << "[Client] Connection to tracker lost during send. Attempting failover..." << std::endl;
        ::close(tracker_sock_fd_);
        tracker_sock_fd_ = -1;
        active_tracker_index_ = (active_tracker_index_ + 1) % trackers_.size();

        if (!connect_to_active_tracker()) {
            return "ERROR: Tracker disconnected and failover failed";
        }
        if (!NetworkUtils::safe_send(tracker_sock_fd_, payload)) {
            return "ERROR: Failed to send command to backup tracker";
        }
    }

    std::string response;
    if (!NetworkUtils::safe_recv_line(tracker_sock_fd_, response, tracker_recv_buffer_, Protocol::DELIMITER)) {
        std::cerr << "[Client] Failed to receive response from tracker." << std::endl;
        ::close(tracker_sock_fd_);
        tracker_sock_fd_ = -1;
        return "ERROR: Tracker closed connection without response";
    }

    return response;
}

void ClientApp::start_peer_listener() {
    peer_server_sock_ = NetworkUtils::create_server_socket(my_ip_, my_port_);
    if (peer_server_sock_ < 0) {
        std::cerr << "[Client] Warning: Could not bind peer-listener to " << my_ip_ << ":" << my_port_ << std::endl;
        return;
    }

    std::cout << "[Client] Peer listener active on " << my_ip_ << ":" << my_port_ << std::endl;
    peer_listener_thread_ = std::thread(&ClientApp::peer_listener_loop, this);
}

void ClientApp::peer_listener_loop() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int peer_fd = ::accept(peer_server_sock_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);

        if (peer_fd < 0) {
            if (!running_) break;
            if (errno == EINTR) continue;
            continue;
        }

        // Handle peer request in detached thread for Phase 2/3 piece requests
        std::thread([this, peer_fd]() {
            this->handle_peer_connection(peer_fd);
        }).detach();
    }
}

void ClientApp::handle_peer_connection(int peer_fd) {
    // Skeleton for peer-to-peer piece transfers
    ::close(peer_fd);
}

bool ClientApp::init() {
    if (!parse_config()) {
        return false;
    }

    running_ = true;
    start_peer_listener();
    connect_to_active_tracker();
    return true;
}

void ClientApp::run() {
    std::string line;
    while (running_) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        line = NetworkUtils::trim_string(line);
        if (line.empty()) continue;

        if (line == "exit" || line == "quit") {
            shutdown();
            break;
        }

        handle_local_command(line);
    }
}

void ClientApp::handle_local_command(const std::string &cmd_line) {
    std::vector<std::string> tokens = NetworkUtils::split_string(cmd_line, ' ');
    if (tokens.empty()) return;

    const std::string &cmd = tokens[0];

    // Local client status commands
    if (cmd == "status") {
        std::cout << "Local Peer Address: " << my_ip_ << ":" << my_port_ << std::endl;
        std::cout << "Active Tracker: Tracker " << (active_tracker_index_ + 1) << " (" 
                  << trackers_[active_tracker_index_].ip << ":" << trackers_[active_tracker_index_].port << ")" << std::endl;
        std::cout << "Logged in as: " << (logged_in_user_.empty() ? "<none>" : logged_in_user_) << std::endl;
        return;
    }

    // Forward to tracker
    std::string res = send_tracker_command(cmd_line);
    std::cout << res << std::endl;

    // Synchronize client session state on successful login or logout
    if (res.rfind(Protocol::RES_SUCCESS, 0) == 0) {
        if (cmd == Protocol::CMD_LOGIN && tokens.size() >= 2) {
            logged_in_user_ = tokens[1];
        } else if (cmd == Protocol::CMD_LOGOUT) {
            logged_in_user_.clear();
        }
    }
}

void ClientApp::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }

    if (peer_server_sock_ >= 0) {
        ::shutdown(peer_server_sock_, SHUT_RDWR);
        ::close(peer_server_sock_);
        peer_server_sock_ = -1;
    }

    if (peer_listener_thread_.joinable()) {
        peer_listener_thread_.join();
    }

    if (tracker_sock_fd_ >= 0) {
        ::close(tracker_sock_fd_);
        tracker_sock_fd_ = -1;
    }

    std::cout << "[Client] Shutdown cleanly." << std::endl;
}
