#include "tracker.h"
#include "../common/network_utils.h"
#include "../common/protocol.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

TrackerServer::TrackerServer(const std::string &info_filepath, int tracker_no)
    : config_path_(info_filepath),
      tracker_no_(tracker_no),
      server_socket_(-1),
      running_(false) {
}

TrackerServer::~TrackerServer() {
    shutdown();
}

bool TrackerServer::parse_config() {
    std::ifstream infile(config_path_);
    if (!infile.is_open()) {
        std::cerr << "[Tracker] Error: Cannot open tracker info file at '" << config_path_ << "'" << std::endl;
        return false;
    }

    std::vector<TrackerEndpoint> endpoints;
    std::string line;
    while (std::getline(infile, line)) {
        line = NetworkUtils::trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        std::string ip;
        int port = 0;
        if (NetworkUtils::parse_ip_port(line, ip, port)) {
            endpoints.push_back({ip, port});
        }
    }

    if (endpoints.size() < 2) {
        std::cerr << "[Tracker] Error: Tracker info file must contain at least 2 tracker endpoints." << std::endl;
        return false;
    }

    if (tracker_no_ < 1 || tracker_no_ > 2) {
        std::cerr << "[Tracker] Error: Invalid tracker number " << tracker_no_ << ". Must be 1 or 2." << std::endl;
        return false;
    }

    my_endpoint_ = endpoints[tracker_no_ - 1];
    peer_endpoint_ = endpoints[(tracker_no_ == 1) ? 1 : 0];

    std::cout << "[Tracker " << tracker_no_ << "] Initialized with IP " 
              << my_endpoint_.ip << ":" << my_endpoint_.port 
              << " (Peer Tracker: " << peer_endpoint_.ip << ":" << peer_endpoint_.port << ")" << std::endl;
    return true;
}

bool TrackerServer::init() {
    if (!parse_config()) {
        return false;
    }

    server_socket_ = NetworkUtils::create_server_socket(my_endpoint_.ip, my_endpoint_.port);
    if (server_socket_ < 0) {
        std::cerr << "[Tracker " << tracker_no_ << "] Error: Failed to bind to " 
                  << my_endpoint_.ip << ":" << my_endpoint_.port << std::endl;
        return false;
    }

    sync_mgr_ = std::make_unique<SyncManager>(tracker_no_, peer_endpoint_.ip, peer_endpoint_.port, user_mgr_, group_mgr_);
    sync_mgr_->start();

    running_ = true;
    return true;
}

void TrackerServer::run() {
    std::cout << "[Tracker " << tracker_no_ << "] Running on port " << my_endpoint_.port 
              << ". Type 'quit' to shutdown." << std::endl;

    console_thread_ = std::thread(&TrackerServer::console_loop, this);

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept(server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);

        if (client_fd < 0) {
            if (!running_) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[Tracker " << tracker_no_ << "] accept() error: " << strerror(errno) << std::endl;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(client_threads_mutex_);
            client_threads_.emplace_back(&TrackerServer::handle_client, this, client_fd);
        }
    }

    if (console_thread_.joinable() && console_thread_.get_id() != std::this_thread::get_id()) {
        console_thread_.join();
    }
}

void TrackerServer::console_loop() {
    std::string input;
    while (running_) {
        if (!std::getline(std::cin, input)) {
            break;
        }
        input = NetworkUtils::trim_string(input);
        if (input == Protocol::CMD_QUIT) {
            std::cout << "[Tracker " << tracker_no_ << "] Quitting tracker..." << std::endl;
            shutdown();
            break;
        }
    }
}

void TrackerServer::shutdown() {
    if (!running_.exchange(false)) {
        return; // Already shutting down
    }

    if (sync_mgr_) {
        sync_mgr_->stop();
    }

    if (server_socket_ >= 0) {
        ::shutdown(server_socket_, SHUT_RDWR);
        ::close(server_socket_);
        server_socket_ = -1;
    }

    // Join all client threads
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_);
        for (auto &t : client_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        client_threads_.clear();
    }

    std::cout << "[Tracker " << tracker_no_ << "] Server stopped cleanly." << std::endl;
}

void TrackerServer::handle_client(int client_fd) {
    std::string buffer;
    std::string line;

    while (running_ && NetworkUtils::safe_recv_line(client_fd, line, buffer, Protocol::DELIMITER)) {
        line = NetworkUtils::trim_string(line);
        if (line.empty()) continue;

        std::string response = process_command(line, client_fd);
        if (!response.empty()) {
            response += Protocol::DELIMITER;
            if (!NetworkUtils::safe_send(client_fd, response)) {
                break;
            }
        }
    }

    // Auto-logout user session on socket disconnect
    user_mgr_.logout_by_fd(client_fd);
    ::close(client_fd);
}

std::string TrackerServer::process_command(const std::string &raw_cmd, int client_fd) {
    std::vector<std::string> tokens = NetworkUtils::split_string(raw_cmd, ' ');
    if (tokens.empty()) return "";

    const std::string &cmd = tokens[0];

    // --- Tracker Internal Synchronization Commands ---
    if (cmd.rfind(Protocol::SYNC_PREFIX, 0) == 0) {
        if (sync_mgr_) {
            sync_mgr_->process_sync_message(raw_cmd, client_fd);
        }
        return "";
    }

    // --- System / Test Commands ---
    if (cmd == Protocol::CMD_PING) {
        return std::string(Protocol::RES_SUCCESS) + " PONG";
    }

    // --- User Management Commands ---
    if (cmd == Protocol::CMD_CREATE_USER) {
        if (tokens.size() != 3) {
            return std::string(Protocol::RES_ERROR) + " Usage: create_user <user_id> <password>";
        }
        std::string err;
        if (user_mgr_.create_user(tokens[1], tokens[2], err)) {
            if (sync_mgr_) {
                sync_mgr_->record_and_broadcast("create_user " + tokens[1] + " " + tokens[2]);
            }
            return std::string(Protocol::RES_SUCCESS) + " User '" + tokens[1] + "' registered successfully";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_LOGIN) {
        if (tokens.size() != 3) {
            return std::string(Protocol::RES_ERROR) + " Usage: login <user_id> <password>";
        }
        std::string err;
        if (user_mgr_.login(tokens[1], tokens[2], client_fd, err)) {
            return std::string(Protocol::RES_SUCCESS) + " User '" + tokens[1] + "' logged in successfully";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_LOGOUT) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " You are not logged in";
        }
        std::string err;
        if (user_mgr_.logout(user_id, err)) {
            return std::string(Protocol::RES_SUCCESS) + " User '" + user_id + "' logged out successfully";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    // --- Group Management Commands ---
    if (cmd == Protocol::CMD_CREATE_GROUP) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " Please login first";
        }
        if (tokens.size() != 2) {
            return std::string(Protocol::RES_ERROR) + " Usage: create_group <group_id>";
        }
        std::string err;
        if (group_mgr_.create_group(tokens[1], user_id, err)) {
            if (sync_mgr_) {
                sync_mgr_->record_and_broadcast("create_group " + tokens[1] + " " + user_id);
            }
            return std::string(Protocol::RES_SUCCESS) + " Group '" + tokens[1] + "' created successfully";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_JOIN_GROUP) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " Please login first";
        }
        if (tokens.size() != 2) {
            return std::string(Protocol::RES_ERROR) + " Usage: join_group <group_id>";
        }
        std::string err;
        if (group_mgr_.join_group(tokens[1], user_id, err)) {
            if (sync_mgr_) {
                sync_mgr_->record_and_broadcast("join_group " + tokens[1] + " " + user_id);
            }
            return std::string(Protocol::RES_SUCCESS) + " Join request submitted for group '" + tokens[1] + "'";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_LEAVE_GROUP) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " Please login first";
        }
        if (tokens.size() != 2) {
            return std::string(Protocol::RES_ERROR) + " Usage: leave_group <group_id>";
        }
        std::string err;
        if (group_mgr_.leave_group(tokens[1], user_id, err)) {
            if (sync_mgr_) {
                sync_mgr_->record_and_broadcast("leave_group " + tokens[1] + " " + user_id);
            }
            return std::string(Protocol::RES_SUCCESS) + " Left group '" + tokens[1] + "' successfully";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_LIST_GROUPS) {
        auto groups = group_mgr_.list_groups();
        if (groups.empty()) {
            return std::string(Protocol::RES_SUCCESS) + " No groups exist in the network";
        }
        std::string res = std::string(Protocol::RES_SUCCESS) + " Groups:";
        for (const auto &g : groups) {
            res += " " + g;
        }
        return res;
    }

    if (cmd == Protocol::CMD_LIST_REQUESTS) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " Please login first";
        }
        if (tokens.size() != 2) {
            return std::string(Protocol::RES_ERROR) + " Usage: list_requests <group_id>";
        }
        std::vector<std::string> reqs;
        std::string err;
        if (group_mgr_.list_requests(tokens[1], user_id, reqs, err)) {
            if (reqs.empty()) {
                return std::string(Protocol::RES_SUCCESS) + " No pending requests for group '" + tokens[1] + "'";
            }
            std::string res = std::string(Protocol::RES_SUCCESS) + " Pending requests:";
            for (const auto &u : reqs) {
                res += " " + u;
            }
            return res;
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    if (cmd == Protocol::CMD_ACCEPT_REQUEST) {
        std::string user_id = user_mgr_.get_user_by_fd(client_fd);
        if (user_id.empty()) {
            return std::string(Protocol::RES_ERROR) + " Please login first";
        }
        if (tokens.size() != 3) {
            return std::string(Protocol::RES_ERROR) + " Usage: accept_request <group_id> <user_id>";
        }
        std::string err;
        if (group_mgr_.accept_request(tokens[1], tokens[2], user_id, err)) {
            if (sync_mgr_) {
                sync_mgr_->record_and_broadcast("accept_request " + tokens[1] + " " + tokens[2]);
            }
            return std::string(Protocol::RES_SUCCESS) + " User '" + tokens[2] + "' accepted into group '" + tokens[1] + "'";
        }
        return std::string(Protocol::RES_ERROR) + " " + err;
    }

    return std::string(Protocol::RES_ERROR) + " Unknown command '" + cmd + "'";
}
