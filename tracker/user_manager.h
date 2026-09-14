#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include <string>
#include <unordered_map>
#include <mutex>

struct User {
    std::string user_id;
    std::string password;
    bool is_logged_in = false;
    int session_fd = -1;
};

class UserManager {
public:
    UserManager() = default;

    // Register a new user. Returns true on success, false if user_id already exists.
    bool create_user(const std::string &user_id, const std::string &password, std::string &err_msg);

    // Authenticate and start a session. Returns true on success.
    bool login(const std::string &user_id, const std::string &password, int client_fd, std::string &err_msg);

    // End session for a user.
    bool logout(const std::string &user_id, std::string &err_msg);

    // End session associated with a socket descriptor (e.g. upon client disconnect).
    void logout_by_fd(int client_fd);

    // Check if user is currently authenticated
    bool is_logged_in(const std::string &user_id);

    // Get user_id currently associated with a client socket
    std::string get_user_by_fd(int client_fd);

    // Check if user exists in the system
    bool user_exists(const std::string &user_id);

    // Apply synchronized user creation from peer tracker
    void apply_sync_create_user(const std::string &user_id, const std::string &password);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, User> users_;          // user_id -> User
    std::unordered_map<int, std::string> fd_to_user_;       // client_fd -> user_id
};

#endif // USER_MANAGER_H
