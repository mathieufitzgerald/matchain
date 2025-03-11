#include <iostream>
#include <string>

// Declarations of special main_ functions from other files
int main_seedNode();
int main_wallet(int argc, char *argv[]);

void initBlockchain();
void startP2P();
void startMining(const std::string &minerPubKeyHash);
void stopMining();

// A simplified main that picks a mode
int main(int argc, char *argv[]) {
    std::string mode = "full";
    if (argc > 1) {
        mode = argv[1];
    }

    if (mode == "--seed") {
        return main_seedNode();
    } else if (mode == "--wallet") {
        return main_wallet(argc, argv);
    } else if (mode == "--miner") {
        initBlockchain();
        startP2P();
        std::cout << "[Miner] Starting miner with dummy pubKeyHash = 'minerKey'" << std::endl;
        startMining("minerKey"); 
        while(true) {
            // run indefinitely, e.g. miner
#ifdef _WIN32
            Sleep(1000);
#else
            usleep(1000*1000);
#endif
        }
        return 0;
    } else {
        // Default: Full node
        std::cout << "[Full Node] Starting full node..." << std::endl;
        initBlockchain();
        startP2P();
        while(true) {
#ifdef _WIN32
            Sleep(1000);
#else
            usleep(1000*1000);
#endif
        }
        return 0;
    }
}
