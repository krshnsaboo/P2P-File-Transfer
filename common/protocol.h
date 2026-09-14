#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

namespace Protocol {
    // Protocol delimiter for message framing
    constexpr char DELIMITER = '\n';
    constexpr const char* DELIMITER_STR = "\n";

    // Standard piece size (512 KB)
    constexpr size_t PIECE_SIZE = 512 * 1024;

    // Command names
    constexpr const char* CMD_CREATE_USER = "create_user";
    constexpr const char* CMD_LOGIN = "login";
    constexpr const char* CMD_LOGOUT = "logout";
    constexpr const char* CMD_CREATE_GROUP = "create_group";
    constexpr const char* CMD_JOIN_GROUP = "join_group";
    constexpr const char* CMD_LEAVE_GROUP = "leave_group";
    constexpr const char* CMD_LIST_GROUPS = "list_groups";
    constexpr const char* CMD_LIST_REQUESTS = "list_requests";
    constexpr const char* CMD_ACCEPT_REQUEST = "accept_request";
    constexpr const char* CMD_UPLOAD_FILE = "upload_file";
    constexpr const char* CMD_LIST_FILES = "list_files";
    constexpr const char* CMD_DOWNLOAD_FILE = "download_file";
    constexpr const char* CMD_SHOW_DOWNLOADS = "show_downloads";
    constexpr const char* CMD_STOP_SHARE = "stop_share";
    constexpr const char* CMD_QUIT = "quit";
    constexpr const char* CMD_PING = "PING";

    // Tracker sync internal commands
    constexpr const char* SYNC_PREFIX = "SYNC_";
    constexpr const char* SYNC_ACK = "SYNC_ACK";

    // Status responses
    constexpr const char* RES_SUCCESS = "SUCCESS";
    constexpr const char* RES_ERROR = "ERROR";
}

#endif // PROTOCOL_H
