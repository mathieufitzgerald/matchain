#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <cstring>
#include <sys/types.h>

#ifdef _WIN32
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/socket.h>
#endif

#include "blockchain_core.cpp"

static std::vector<std::string> g_knownPeers;
static std::mutex g_peersMutex;

// Cross-platform sleep
static void sleepMilliseconds(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static int createSocket(uint16_t port) {
#ifdef _WIN32
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
        wsaInitialized = true;
    }
#endif
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Could not create socket!" << std::endl;
        return -1;
    }
    // Bind
    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    int optval = 1;
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    if (bind(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Bind failed on port " << port << std::endl;
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return -1;
    }
    if (listen(sockfd, 8) < 0) {
        std::cerr << "Listen failed on port " << port << std::endl;
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return -1;
    }
    return sockfd;
}

// Peer handler
static void handleClient(int clientSock) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, 1024);
        int bytesRead = recv(clientSock, buffer, 1023, 0);
        if (bytesRead <= 0) {
            break;
        }
        // In a real protocol, you'd parse messages here
        // For demonstration, let's just print it
        std::string msg(buffer, bytesRead);
        std::cout << "[P2P] Received: " << msg << std::endl;
    }
#ifdef _WIN32
    closesocket(clientSock);
#else
    close(clientSock);
#endif
}

// Node listening for inbound connections
static void listenForPeers(uint16_t port) {
    int serverSock = createSocket(port);
    if (serverSock < 0) {
        std::cerr << "Failed to open server socket on port " << port << std::endl;
        return;
    }
    std::cout << "[P2P] Listening on port " << port << std::endl;

    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSock < 0) {
            continue;
        }
        std::thread t(handleClient, clientSock);
        t.detach();
    }
}

// Connect to a peer
static void connectToPeer(const std::string &peerAddrStr) {
    size_t colonPos = peerAddrStr.find(':');
    std::string ip = peerAddrStr.substr(0, colonPos);
    int port = std::stoi(peerAddrStr.substr(colonPos+1));

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return;

    sockaddr_in peerAddr;
    memset(&peerAddr, 0, sizeof(peerAddr));
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &peerAddr.sin_addr);

    if (connect(sockfd, (sockaddr*)&peerAddr, sizeof(peerAddr)) < 0) {
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return;
    }

    // Add to known peers
    {
        std::lock_guard<std::mutex> lock(g_peersMutex);
        g_knownPeers.push_back(peerAddrStr);
    }
    std::cout << "[P2P] Connected to peer " << peerAddrStr << std::endl;

    // Optionally spawn a thread to read data from this peer
    std::thread t([sockfd, peerAddrStr]() {
        char buffer[1024];
        while (true) {
            memset(buffer, 0, 1024);
            int bytesRead = recv(sockfd, buffer, 1023, 0);
            if (bytesRead <= 0) {
                break;
            }
            std::string msg(buffer, bytesRead);
            std::cout << "[P2P] From " << peerAddrStr << " >> " << msg << std::endl;
        }
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
    });
    t.detach();
}

// Periodically connect to known seed nodes
static void discoveryLoop(const Json::Value &config) {
    Json::Value seeds = config["seedNodes"];
    while (true) {
        for (auto &seed : seeds) {
            connectToPeer(seed.asString());
        }
        // Sleep then try again
        sleepMilliseconds(30000);
    }
}

// Start the P2P system
void startP2P() {
    Json::Value cfg = loadConfig("config.json");
    uint16_t port = cfg.get("p2pPort", 8333).asUInt();
    // Start listening
    std::thread t(listenForPeers, port);
    t.detach();

    // Start discovery
    std::thread t2(discoveryLoop, cfg);
    t2.detach();
}
