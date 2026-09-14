#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <string>
#include <vector>
#include <cstddef>

namespace NetworkUtils {
    // Safely send all data over a socket, handling partial writes and EINTR
    bool safe_send(int sock_fd, const std::string &data);
    bool safe_send_bytes(int sock_fd, const void *buf, size_t len);

    // Safely read a single delimiter-terminated line from socket with a persistent receive buffer
    bool safe_recv_line(int sock_fd, std::string &out_line, std::string &buffer, char delim = '\n');

    // Safely read exactly len bytes from socket into buffer
    bool safe_recv_bytes(int sock_fd, void *buf, size_t len);

    // Create, bind, and listen on a TCP server socket (sets SO_REUSEADDR)
    int create_server_socket(const std::string &ip, int port, int backlog = 64);

    // Connect to a remote server with timeout
    int connect_to_server(const std::string &ip, int port, int timeout_sec = 3);

    // Parse "IP:PORT" or "IP PORT" strings
    bool parse_ip_port(const std::string &endpoint, std::string &ip, int &port);

    // String utility helpers
    std::vector<std::string> split_string(const std::string &str, char delim = ' ');
    std::string trim_string(const std::string &str);
}

#endif // NETWORK_UTILS_H
