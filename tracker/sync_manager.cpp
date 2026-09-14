#include "sync_manager.h"
#include "user_manager.h"
#include "group_manager.h"
#include "../common/network_utils.h"
#include "../common/protocol.h"

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <chrono>

SyncManager::SyncManager(int my_tracker_no, const std::string &peer_ip, int peer_port,
                         UserManager &user_mgr, GroupManager &group_mgr)
    : my_tracker_no_(my_tracker_no),
      peer_ip_(peer_ip),
      peer_port_(peer_port),
      user_mgr_(user_mgr),
      group_mgr_(group_mgr),
      running_(false),
      peer_online_(false),
      outbound_fd_(-1),
      current_seq_id_(0),
      last_received_seq_id_(0) {
}

SyncManager::~SyncManager() {
    stop();
}

void SyncManager::start() {
    running_ = true;
    sync_client_thread_ = std::thread(&SyncManager::sync_client_loop, this);
}

void SyncManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (outbound_fd_ >= 0) {
            ::shutdown(outbound_fd_, SHUT_RDWR);
            ::close(outbound_fd_);
            outbound_fd_ = -1;
        }
    }

    if (sync_client_thread_.joinable()) {
        sync_client_thread_.join();
    }
}

void SyncManager::record_and_broadcast(const std::string &mutation_cmd) {
    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        seq = ++current_seq_id_;
        event_log_.push_back({seq, mutation_cmd});
    }

    // Broadcast mutation to peer tracker if connected
    std::string payload = std::string(Protocol::CMD_SYNC_MUTATION) + " " 
                        + std::to_string(seq) + " " + mutation_cmd + Protocol::DELIMITER;
    send_to_peer(payload);
}

void SyncManager::send_to_peer(const std::string &payload) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (peer_online_ && outbound_fd_ >= 0) {
        if (!NetworkUtils::safe_send(outbound_fd_, payload)) {
            peer_online_ = false;
        }
    }
}

void SyncManager::sync_client_loop() {
    while (running_) {
        int fd = NetworkUtils::connect_to_server(peer_ip_, peer_port_, 2);
        if (fd < 0) {
            peer_online_ = false;
            // Sleep and retry connecting to peer tracker
            for (int i = 0; i < 20 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            outbound_fd_ = fd;
            peer_online_ = true;
        }

        std::cout << "[Tracker " << my_tracker_no_ << "] Established replication link to peer tracker at "
                  << peer_ip_ << ":" << peer_port_ << std::endl;

        // Initiate handshake and request state catch-up
        uint64_t last_known = last_received_seq_id_.load();
        std::string handshake = std::string(Protocol::CMD_SYNC_HANDSHAKE) + " " 
                              + std::to_string(my_tracker_no_) + " " + std::to_string(last_known) 
                              + Protocol::DELIMITER;
        std::string recover = std::string(Protocol::CMD_SYNC_RECOVER) + " " 
                            + std::to_string(last_known) + Protocol::DELIMITER;

        NetworkUtils::safe_send(fd, handshake);
        NetworkUtils::safe_send(fd, recover);

        // Receive stream of sync events from peer
        std::string buffer;
        std::string line;
        while (running_ && NetworkUtils::safe_recv_line(fd, line, buffer, Protocol::DELIMITER)) {
            line = NetworkUtils::trim_string(line);
            if (line.empty()) continue;
            process_sync_message(line, fd);
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (outbound_fd_ >= 0) {
                ::close(outbound_fd_);
                outbound_fd_ = -1;
            }
            peer_online_ = false;
        }

        std::cout << "[Tracker " << my_tracker_no_ << "] Lost replication link to peer tracker. Will retry..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void SyncManager::process_sync_message(const std::string &msg, int source_fd) {
    std::vector<std::string> tokens = NetworkUtils::split_string(msg, ' ');
    if (tokens.empty()) return;

    const std::string &cmd = tokens[0];

    if (cmd == Protocol::CMD_SYNC_MUTATION || cmd == Protocol::CMD_SYNC_REPLAY) {
        if (tokens.size() < 3) return;

        uint64_t seq = 0;
        try {
            seq = std::stoull(tokens[1]);
            uint64_t prev = last_received_seq_id_.load();
            if (seq > prev) {
                last_received_seq_id_.store(seq);
            }
        } catch (...) {
            return;
        }

        // Reconstruct mutation command payload (from token index 2 onwards)
        std::string mutation;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (i > 2) mutation += " ";
            mutation += tokens[i];
        }

        // Record into local WAL log so we can replay to other trackers on recovery
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            event_log_.push_back({seq, mutation});
            if (seq > current_seq_id_) {
                current_seq_id_ = seq;
            }
        }

        apply_mutation(mutation);
    } else if (cmd == Protocol::CMD_SYNC_RECOVER) {
        if (tokens.size() >= 2) {
            try {
                uint64_t since_seq = std::stoull(tokens[1]);
                replay_events_since(since_seq, source_fd);
            } catch (...) {}
        }
    } else if (cmd == Protocol::CMD_SYNC_REPLAY_DONE) {
        std::cout << "[Tracker " << my_tracker_no_ << "] State catch-up from peer tracker completed." << std::endl;
    }
}

void SyncManager::apply_mutation(const std::string &mutation_cmd) {
    std::vector<std::string> tokens = NetworkUtils::split_string(mutation_cmd, ' ');
    if (tokens.empty()) return;

    const std::string &op = tokens[0];

    if (op == Protocol::CMD_CREATE_USER && tokens.size() == 3) {
        user_mgr_.apply_sync_create_user(tokens[1], tokens[2]);
        std::cout << "[Sync " << my_tracker_no_ << "] Replicated user: " << tokens[1] << std::endl;
    } else if (op == Protocol::CMD_CREATE_GROUP && tokens.size() == 3) {
        group_mgr_.apply_sync_create_group(tokens[1], tokens[2]);
        std::cout << "[Sync " << my_tracker_no_ << "] Replicated group: " << tokens[1] << " (Owner: " << tokens[2] << ")" << std::endl;
    } else if (op == Protocol::CMD_JOIN_GROUP && tokens.size() == 3) {
        group_mgr_.apply_sync_join_group(tokens[1], tokens[2]);
        std::cout << "[Sync " << my_tracker_no_ << "] Replicated join request: user " << tokens[2] << " -> group " << tokens[1] << std::endl;
    } else if (op == Protocol::CMD_ACCEPT_REQUEST && tokens.size() == 3) {
        group_mgr_.apply_sync_accept_request(tokens[1], tokens[2]);
        std::cout << "[Sync " << my_tracker_no_ << "] Replicated accepted request: user " << tokens[2] << " -> group " << tokens[1] << std::endl;
    } else if (op == Protocol::CMD_LEAVE_GROUP && tokens.size() == 3) {
        group_mgr_.apply_sync_leave_group(tokens[1], tokens[2]);
        std::cout << "[Sync " << my_tracker_no_ << "] Replicated leave group: user " << tokens[2] << " -> group " << tokens[1] << std::endl;
    }
}

void SyncManager::replay_events_since(uint64_t since_seq, int target_fd) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    for (const auto &ev : event_log_) {
        if (ev.seq_id > since_seq) {
            std::string payload = std::string(Protocol::CMD_SYNC_REPLAY) + " " 
                                + std::to_string(ev.seq_id) + " " + ev.command 
                                + Protocol::DELIMITER;
            NetworkUtils::safe_send(target_fd, payload);
        }
    }

    std::string done_payload = std::string(Protocol::CMD_SYNC_REPLAY_DONE) + " " 
                             + std::to_string(current_seq_id_.load()) + Protocol::DELIMITER;
    NetworkUtils::safe_send(target_fd, done_payload);
}

bool SyncManager::is_peer_online() const {
    return peer_online_.load();
}

uint64_t SyncManager::get_latest_seq() const {
    return current_seq_id_.load();
}
