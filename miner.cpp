#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <openssl/sha.h>
#include "blockchain_core.cpp" // or a separate header if you prefer

static std::atomic<bool> g_mining{false};

static std::string sha256(const std::string &input); // forward

// The simple miner thread function
void mineBlock(const std::string &minerPubKeyHash) {
    Blockchain *chain = getBlockchain();
    while (g_mining.load()) {
        // Create a new block with coinbase
        Block newBlock = chain->createNewBlock(minerPubKeyHash);
        // Attempt PoW
        newBlock.buildMerkleRoot();

        // PoW loop
        while (true) {
            if (!g_mining.load()) break;

            std::string blockHash = newBlock.getBlockHash();
            // Check if blockHash < difficulty
            if (chain->isValidProofOfWork(newBlock)) {
                // Found a valid block
                if (chain->addBlock(newBlock)) {
                    std::cout << "[Miner] Found a new block! Hash: " << blockHash << std::endl;
                } else {
                    std::cout << "[Miner] Block was rejected. Possibly a race condition." << std::endl;
                }
                break;
            } else {
                newBlock.header.nonce++;
            }
        }
        // Sleep a bit before building a new block again
        // In reality, you'd continuously update block time, transaction set, etc.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Start the mining process in a background thread
void startMining(const std::string &minerPubKeyHash) {
    g_mining.store(true);
    std::thread t([minerPubKeyHash]() {
        mineBlock(minerPubKeyHash);
    });
    t.detach();
}

// Stop the miner
void stopMining() {
    g_mining.store(false);
}
