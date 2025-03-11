# MyCoin - A Proof-of-Concept Cryptocurrency

## Overview
MyCoin is a test cryptocurrency with a **Bitcoin-like architecture**: it uses a UTXO model, SHA-256 Proof-of-Work mining, block reward halving, and a peer-to-peer (P2P) network protocol for block/transaction propagation.

**DISCLAIMER**: This code is a proof-of-concept and **not** intended for production use without further security auditing, testing, and development. Use at your own risk.

## Features
- Full Blockchain Node (with UTXO set, block/transaction verification).
- Proof-of-Work Miner (CPU mining).
- P2P Network for Node Discovery and Synchronization (TCP-based).
- Seed Node for bootstrapping new nodes.
- Wallet with GUI (Qt) supporting:
  - ECDSA (secp256k1) for key generation and signing
  - Hierarchical Deterministic (HD) key structure (similar to BIP-32)
  - Placeholder for BIP-39 Mnemonic Seeds
- REST/JSON-RPC skeleton in place for advanced usage.

## Dependencies
- C++17 or newer
- OpenSSL (for SHA-256, ECDSA)
- Qt 5+ (for the Wallet GUI)
- A recent compiler (GCC, Clang, or MSVC)
- [Optional] A BIP-39 library and a wordlist file if you want full mnemonic support.

## Building
1. Ensure you have OpenSSL, Qt, and standard build tools installed.
2. Create a directory and clone or copy all `.cpp` files (and this `config.json`) into it.
3. Create a simple `CMakeLists.txt` similar to:
   ```cmake
   cmake_minimum_required(VERSION 3.10)
   project(MyCoin)

   set(CMAKE_CXX_STANDARD 17)

   find_package(OpenSSL REQUIRED)
   find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)

   include_directories(${OPENSSL_INCLUDE_DIR})

   set(SOURCES
       blockchain_core.cpp
       network_protocol.cpp
       seed_node.cpp
       miner.cpp
       wallet.cpp
   )

   add_executable(mycoin ${SOURCES})
   target_link_libraries(mycoin
       Qt5::Core
       Qt5::Widgets
       Qt5::Network
       ${OPENSSL_LIBRARIES}
   )
