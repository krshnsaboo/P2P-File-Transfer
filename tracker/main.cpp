#include "tracker.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <tracker_info.txt> <tracker_no>" << std::endl;
        std::cerr << "Example: " << argv[0] << " tracker_info.txt 1" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];
    int tracker_no = 0;
    try {
        tracker_no = std::stoi(argv[2]);
    } catch (...) {
        std::cerr << "Error: tracker_no must be an integer (1 or 2)." << std::endl;
        return 1;
    }

    TrackerServer server(config_path, tracker_no);
    if (!server.init()) {
        return 1;
    }

    server.run();
    return 0;
}
