#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <mutex>
#include <fstream>
#include <map>
#include <cstdint>
#include <openssl/sha.h>
#include <algorithm>
#include <jsoncpp/json/json.h>

// ------------------- GLOBAL CONFIG / STRUCTS -------------------
static std::mutex g_blockchainMutex; // For thread safety around blockchain

// Basic function to load config.json:
static Json::Value loadConfig(const std::string &filename) {
    Json::Value config;
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open config.json. Using defaults." << std::endl;
        return config;
    }
    Json::Reader reader;
    if (!reader.parse(ifs, config)) {
        std::cerr << "Failed to parse config.json. Using defaults." << std::endl;
    }
    ifs.close();
    return config;
}

// Simple SHA-256 wrapper using OpenSSL
static std::string sha256(const std::string &input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Merkle root calculation for a list of transaction hashes
static std::string calculateMerkleRoot(const std::vector<std::string> &txHashes) {
    if (txHashes.empty()) {
        return std::string(SHA256_DIGEST_LENGTH * 2, '0');
    }

    std::vector<std::string> currentLevel = txHashes;
    while (currentLevel.size() > 1) {
        if (currentLevel.size() % 2 != 0) {
            currentLevel.push_back(currentLevel.back());
        }
        std::vector<std::string> newLevel;
        for (size_t i = 0; i < currentLevel.size(); i += 2) {
            newLevel.push_back(sha256(currentLevel[i] + currentLevel[i + 1]));
        }
        currentLevel = newLevel;
    }
    return currentLevel.front();
}

// Represents an input to a transaction, referencing a previous tx's output
struct TxInput {
    std::string txid;  // The transaction hash that this input references
    uint32_t index;    // Which output index of the previous tx is used
    std::string signature;  // ECDSA signature of the input (placeholder)

    std::string toString() const {
        std::stringstream ss;
        ss << txid << index << signature;
        return ss.str();
    }
};

// Represents an output from a transaction, specifying the amount and "locking script"
struct TxOutput {
    uint64_t amount;           // Amount in "satoshis"
    std::string pubKeyHash;    // Simplified "scriptPubKey" (hash of public key)

    std::string toString() const {
        std::stringstream ss;
        ss << amount << pubKeyHash;
        return ss.str();
    }
};

// Represents a transaction with multiple inputs and outputs
class Transaction {
public:
    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;
    uint32_t version;
    uint32_t lockTime; // not fully used in this PoC

    // For quick identification
    std::string getTxId() const {
        // We'll hash the entire transaction data
        std::stringstream ss;
        ss << version << lockTime;
        for (auto &in : inputs) {
            ss << in.toString();
        }
        for (auto &out : outputs) {
            ss << out.toString();
        }
        return sha256(ss.str());
    }
};

// Represents a block header, separate from the transactions themselves
struct BlockHeader {
    uint32_t version;
    std::string prevBlockHash;
    std::string merkleRoot;
    uint64_t timestamp;
    uint32_t difficultyTarget;
    uint64_t nonce;
};

// Represents a full block
class Block {
public:
    BlockHeader header;
    std::vector<Transaction> transactions;

    // Return the block hash
    std::string getBlockHash() const {
        std::stringstream ss;
        ss << header.version
           << header.prevBlockHash
           << header.merkleRoot
           << header.timestamp
           << header.difficultyTarget
           << header.nonce;
        return sha256(ss.str());
    }

    // Construct merkle root from this block's transactions
    void buildMerkleRoot() {
        std::vector<std::string> txHashes;
        for (auto &tx : transactions) {
            txHashes.push_back(tx.getTxId());
        }
        header.merkleRoot = calculateMerkleRoot(txHashes);
    }
};

// In a real system, the UTXO set is typically a LevelDB or RocksDB database on disk.
// For this proof-of-concept, we'll keep it in memory in a map: (txid:index) -> (amount, pubKeyHash).
struct UTXO {
    uint64_t amount;
    std::string pubKeyHash;
};

static std::map<std::string, UTXO> g_utxoSet;

static uint64_t g_totalBlocks = 0; // Track how many blocks are in the chain

// The main Blockchain manager
class Blockchain {
private:
    std::vector<Block> chain; 
    Json::Value config;

    uint64_t blockReward;
    uint64_t blockHalvingInterval;
    uint32_t targetSpacing; // seconds
    uint32_t difficultyTarget; // we use a simplified difficulty mechanism

public:
    Blockchain(const Json::Value &cfg)
        : config(cfg) {
        // Load config
        blockReward = cfg.get("blockReward", 50).asUInt64();
        blockHalvingInterval = cfg.get("blockHalvingInterval", 210000).asUInt64();
        targetSpacing = cfg.get("targetSpacing", 600).asUInt();
        difficultyTarget = 0x1f00ffff; // A simplistic placeholder

        // Build or load genesis block
        if (chain.empty()) {
            Block genesis = createGenesisBlock(cfg.get("genesisMessage", "Hello from Genesis!").asString());
            chain.push_back(genesis);
            g_totalBlocks = 1;
            // Add coinbase UTXO from genesis
            const Transaction &coinbaseTx = genesis.transactions.front();
            for (size_t i = 0; i < coinbaseTx.outputs.size(); i++) {
                std::string outKey = coinbaseTx.getTxId() + ":" + std::to_string(i);
                UTXO utxo{coinbaseTx.outputs[i].amount, coinbaseTx.outputs[i].pubKeyHash};
                g_utxoSet[outKey] = utxo;
            }
        }
    }

    Block createGenesisBlock(const std::string &msg) {
        Block genesis;
        genesis.header.version = 1;
        genesis.header.prevBlockHash = std::string(64, '0');
        genesis.header.timestamp = static_cast<uint64_t>(std::time(nullptr));
        genesis.header.difficultyTarget = difficultyTarget;
        genesis.header.nonce = 0;

        Transaction coinbaseTx;
        coinbaseTx.version = 1;
        coinbaseTx.lockTime = 0;
        // No real inputs
        TxInput in;
        in.txid = "0";
        in.index = 0;
        in.signature = msg; // embed message in coinbase
        coinbaseTx.inputs.push_back(in);

        // One output to an arbitrary "pubKeyHash"
        TxOutput out;
        out.amount = blockReward * 100000000; // 50 coins in satoshi
        out.pubKeyHash = sha256("genesis-pubkey"); 
        coinbaseTx.outputs.push_back(out);

        genesis.transactions.push_back(coinbaseTx);
        genesis.buildMerkleRoot();

        return genesis;
    }

    // Return the most recent block
    Block getLatestBlock() const {
        return chain.back();
    }

    // Return entire chain
    const std::vector<Block>& getChain() const {
        return chain;
    }

    // Add a new block to the chain (after validation)
    bool addBlock(const Block &newBlock) {
        // Basic checks
        std::string prevHash = newBlock.header.prevBlockHash;
        std::string latestHash = getLatestBlock().getBlockHash();

        if (prevHash != latestHash) {
            std::cerr << "[Blockchain] Rejecting block: prevHash mismatch" << std::endl;
            return false;
        }
        // Validate PoW
        if (!isValidProofOfWork(newBlock)) {
            std::cerr << "[Blockchain] Rejecting block: invalid PoW" << std::endl;
            return false;
        }
        // Validate transactions, update UTXO set
        if (!validateAndApplyTransactions(newBlock.transactions)) {
            std::cerr << "[Blockchain] Rejecting block: invalid transaction(s)" << std::endl;
            return false;
        }

        // Everything is good
        {
            std::lock_guard<std::mutex> lock(g_blockchainMutex);
            chain.push_back(newBlock);
            g_totalBlocks++;
        }
        return true;
    }

    // Check the block's hash is below the difficulty target
    bool isValidProofOfWork(const Block &block) {
        // Construct target from block.header.difficultyTarget
        // For simplicity, we interpret difficultyTarget as a 32-bit "network difficulty bits" 
        // but we won't fully replicate the Bitcoin alg. We'll just do a numeric compare.

        std::string hash = block.getBlockHash();
        // Compare the first few hex digits
        // E.g., difficultyTarget=0x1f00ffff might require the first 4 hex digits to be zero
        // This is naive, but workable for demonstration.

        // Let's say we require the first 4 hex chars to be '0'
        // (You can do more elaborate decode of 'bits' for a real system.)
        for (int i = 0; i < 4; i++) {
            if (hash[i] != '0') {
                return false;
            }
        }
        return true;
    }

    // Validate each transaction, ensure no double spends, correct signatures, etc.
    bool validateAndApplyTransactions(const std::vector<Transaction> &transactions) {
        for (auto &tx : transactions) {
            if (!validateTransaction(tx)) {
                return false;
            }
            // Update UTXO set
            applyTransaction(tx);
        }
        return true;
    }

    bool validateTransaction(const Transaction &tx) {
        // Check inputs are unspent, signatures valid (placeholder check)
        // Also ensure sum(inputs) >= sum(outputs)
        uint64_t inputSum = 0;
        for (auto &in : tx.inputs) {
            std::string key = in.txid + ":" + std::to_string(in.index);
            // Must exist in UTXO
            if (g_utxoSet.find(key) == g_utxoSet.end()) {
                std::cerr << "Double spend or missing UTXO for " << key << std::endl;
                return false;
            }
            // In real code, also verify the signature matches the pubKeyHash in g_utxoSet[key]
            inputSum += g_utxoSet[key].amount;
        }

        uint64_t outputSum = 0;
        for (auto &out : tx.outputs) {
            outputSum += out.amount;
        }

        if (outputSum > inputSum) {
            std::cerr << "Output sum exceeds input sum" << std::endl;
            return false;
        }
        return true;
    }

    void applyTransaction(const Transaction &tx) {
        // Remove spent UTXOs
        for (auto &in : tx.inputs) {
            std::string key = in.txid + ":" + std::to_string(in.index);
            g_utxoSet.erase(key);
        }
        // Create new UTXOs
        for (size_t i = 0; i < tx.outputs.size(); i++) {
            std::string outKey = tx.getTxId() + ":" + std::to_string(i);
            UTXO utxo{tx.outputs[i].amount, tx.outputs[i].pubKeyHash};
            g_utxoSet[outKey] = utxo;
        }
    }

    // Return next block reward, with halving logic
    uint64_t getBlockReward() const {
        uint64_t halvings = g_totalBlocks / blockHalvingInterval;
        if (halvings >= 64) {
            return 0; // Once it halves enough times, it's effectively zero
        }
        return blockReward >> halvings;
    }

    // Return the current difficulty target
    uint32_t getDifficultyTarget() const {
        // Could adjust every X blocks based on timestamps, etc.
        // We'll just keep it constant for simplicity or do minor modifications.
        return difficultyTarget;
    }

    // Create a new block with a coinbase transaction (reward + optional fees)
    Block createNewBlock(const std::string &minerPubKeyHash) {
        Block newBlock;
        newBlock.header.version = 1;
        newBlock.header.prevBlockHash = getLatestBlock().getBlockHash();
        newBlock.header.timestamp = static_cast<uint64_t>(std::time(nullptr));
        newBlock.header.difficultyTarget = getDifficultyTarget();
        newBlock.header.nonce = 0;

        // Coinbase Tx
        Transaction coinbaseTx;
        coinbaseTx.version = 1;
        coinbaseTx.lockTime = 0;
        TxInput coinbaseIn;
        coinbaseIn.txid = "0";
        coinbaseIn.index = 0;
        coinbaseIn.signature = "coinbase"; 
        coinbaseTx.inputs.push_back(coinbaseIn);

        TxOutput coinbaseOut;
        coinbaseOut.amount = getBlockReward() * 100000000ULL;
        coinbaseOut.pubKeyHash = minerPubKeyHash;
        coinbaseTx.outputs.push_back(coinbaseOut);

        newBlock.transactions.push_back(coinbaseTx);
        return newBlock;
    }
};

// Global pointer to the main blockchain instance
static Blockchain *g_blockchain = nullptr;

// Provide access to the global blockchain
Blockchain* getBlockchain() {
    return g_blockchain;
}

// Initialize global blockchain
void initBlockchain() {
    Json::Value cfg = loadConfig("config.json");
    static Blockchain chain(cfg);
    g_blockchain = &chain;
}
