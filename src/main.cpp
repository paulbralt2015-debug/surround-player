#include "player.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <video_file>\n"
                  << "Controls:\n"
                  << "  S     - Toggle Surround (Pro Logic II)\n"
                  << "  ESC   - Quit\n";
        return 1;
    }

    try {
        Player player;
        if (!player.open(argv[1])) {
            std::cerr << "Failed to open file.\n";
            return 1;
        }
        player.play();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
