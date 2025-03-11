#include <iostream>
#include "blockchain_core.cpp"
#include "network_protocol.cpp"

// Basic seed node. Could be run with: ./mycoin --seed
int main_seedNode() {
    std::cout << "[Seed Node] Starting seed node..." << std::endl;
    initBlockchain();  // if you want it also to hold the blockchain
    startP2P();
    // Keep running
    while(true) {
        // Sleep
#ifdef _WIN32
        Sleep(1000);
#else
        usleep(1000*1000);
#endif
    }
    return 0;
}
