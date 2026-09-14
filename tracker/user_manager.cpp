#include "user_manager.h"

bool UserManager::create_user(const std::string &user_id, const std::string &password, std::string &err_msg) {
    if (user_id.empty() || password.empty()) {
        err_msg = "User ID and password cannot be empty";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.find(user_id) != users_.end()) {
        err_msg = "User already exists";
        return false;
    }

    User u;
    u.user_id = user_id;
    u.password = password;
    u.is_logged_in = false;
    u.session_fd = -1;
    users_[user_id] = u;

    return true;
}

bool UserManager::login(const std::string &user_id, const std::string &password, int client_fd, std::string &err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    if (it == users_.end()) {
        err_msg = "User not found";
        return false;
    }

    if (it->second.password != password) {
        err_msg = "Invalid password";
        return false;
    }

    if (it->second.is_logged_in) {
        err_msg = "User already logged in";
        return false;
    }

    // Check if this socket is already logged in as another user
    auto fd_it = fd_to_user_.find(client_fd);
    if (fd_it != fd_to_user_.end() && !fd_it->second.empty()) {
        err_msg = "This session is already logged in as '" + fd_it->second + "'. Please logout first.";
        return false;
    }

    it->second.is_logged_in = true;
    it->second.session_fd = client_fd;
    fd_to_user_[client_fd] = user_id;

    return true;
}

bool UserManager::logout(const std::string &user_id, std::string &err_msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    if (it == users_.end()) {
        err_msg = "User not found";
        return false;
    }

    if (!it->second.is_logged_in) {
        err_msg = "User is not logged in";
        return false;
    }

    if (it->second.session_fd >= 0) {
        fd_to_user_.erase(it->second.session_fd);
    }
    it->second.is_logged_in = false;
    it->second.session_fd = -1;

    return true;
}

void UserManager::logout_by_fd(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = fd_to_user_.find(client_fd);
    if (it != fd_to_user_.end()) {
        const std::string &uid = it->second;
        auto user_it = users_.find(uid);
        if (user_it != users_.end()) {
            user_it->second.is_logged_in = false;
            user_it->second.session_fd = -1;
        }
        fd_to_user_.erase(it);
    }
}

bool UserManager::is_logged_in(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(user_id);
    if (it != users_.end()) {
        return it->second.is_logged_in;
    }
    return false;
}

std::string UserManager::get_user_by_fd(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fd_to_user_.find(client_fd);
    if (it != fd_to_user_.end()) {
        return it->second;
    }
    return "";
}

bool UserManager::user_exists(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.find(user_id) != users_.end();
}

void UserManager::apply_sync_create_user(const std::string &user_id, const std::string &password) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.find(user_id) == users_.end()) {
        User u;
        u.user_id = user_id;
        u.password = password;
        u.is_logged_in = false;
        u.session_fd = -1;
        users_[user_id] = u;
    }
}

