#include "client.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <IP>:<PORT> <tracker_info.txt>" << std::endl;
        std::cerr << "Example: " << argv[0] << " 127.0.0.1:9000 tracker_info.txt" << std::endl;
        return 1;
    }

    std::string peer_endpoint = argv[1];
    std::string tracker_info_path = argv[2];

    ClientApp client(peer_endpoint, tracker_info_path);
    if (!client.init()) {
        return 1;
    }

    client.run();
    return 0;
}
