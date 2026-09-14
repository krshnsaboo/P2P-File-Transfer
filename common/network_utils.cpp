#include "network_utils.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace NetworkUtils {

bool safe_send_bytes(int sock_fd, const void *buf, size_t len) {
    const char *ptr = static_cast<const char*>(buf);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = ::send(sock_fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false; // Connection closed
        }
        remaining -= sent;
        ptr += sent;
    }
    return true;
}

bool safe_send(int sock_fd, const std::string &data) {
    return safe_send_bytes(sock_fd, data.data(), data.size());
}

bool safe_recv_line(int sock_fd, std::string &out_line, std::string &buffer, char delim) {
    while (true) {
        size_t pos = buffer.find(delim);
        if (pos != std::string::npos) {
            out_line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            return true;
        }

        char temp[2048];
        ssize_t bytes_read = ::recv(sock_fd, temp, sizeof(temp), 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes_read == 0) {
            // Socket closed. If any buffer remains, return it as final line
            if (!buffer.empty()) {
                out_line = buffer;
                buffer.clear();
                return true;
            }
            return false;
        }
        buffer.append(temp, bytes_read);
    }
}

bool safe_recv_bytes(int sock_fd, void *buf, size_t len) {
    char *ptr = static_cast<char*>(buf);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t bytes_read = ::recv(sock_fd, ptr, remaining, 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes_read == 0) {
            return false; // EOF before expected length
        }
        remaining -= bytes_read;
        ptr += bytes_read;
    }
    return true;
}

int create_server_socket(const std::string &ip, int port, int backlog) {
    int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[NetworkUtils] socket() failed: " << strerror(errno) << std::endl;
        return -1;
    }

    int opt = 1;
    if (::setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[NetworkUtils] setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
        ::close(sock_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (ip.empty() || ip == "0.0.0.0") {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "[NetworkUtils] inet_pton() failed for IP: " << ip << std::endl;
            ::close(sock_fd);
            return -1;
        }
    }

    if (::bind(sock_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "[NetworkUtils] bind() failed on " << ip << ":" << port << " - " << strerror(errno) << std::endl;
        ::close(sock_fd);
        return -1;
    }

    if (::listen(sock_fd, backlog) < 0) {
        std::cerr << "[NetworkUtils] listen() failed: " << strerror(errno) << std::endl;
        ::close(sock_fd);
        return -1;
    }

    return sock_fd;
}

int connect_to_server(const std::string &ip, int port, int timeout_sec) {
    int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return -1;
    }

    // Set non-blocking mode for connect timeout
    int flags = ::fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(sock_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        ::close(sock_fd);
        return -1;
    }

    int res = ::connect(sock_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
    if (res < 0) {
        if (errno != EINPROGRESS) {
            ::close(sock_fd);
            return -1;
        }

        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock_fd, &write_fds);

        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;

        int select_res = ::select(sock_fd + 1, nullptr, &write_fds, nullptr, &tv);
        if (select_res <= 0) {
            // Timeout or error
            ::close(sock_fd);
            return -1;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            ::close(sock_fd);
            return -1;
        }
    }

    // Restore blocking mode
    ::fcntl(sock_fd, F_SETFL, flags);
    return sock_fd;
}

bool parse_ip_port(const std::string &endpoint, std::string &ip, int &port) {
    size_t colon_pos = endpoint.find(':');
    if (colon_pos != std::string::npos) {
        ip = endpoint.substr(0, colon_pos);
        try {
            port = std::stoi(endpoint.substr(colon_pos + 1));
            return (port > 0 && port <= 65535);
        } catch (...) {
            return false;
        }
    }

    size_t space_pos = endpoint.find(' ');
    if (space_pos != std::string::npos) {
        ip = endpoint.substr(0, space_pos);
        try {
            port = std::stoi(endpoint.substr(space_pos + 1));
            return (port > 0 && port <= 65535);
        } catch (...) {
            return false;
        }
    }

    return false;
}

std::vector<std::string> split_string(const std::string &str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string trim_string(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

} // namespace NetworkUtils
