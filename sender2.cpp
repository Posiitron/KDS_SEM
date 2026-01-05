#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <vector>
#include <deque>
#include <chrono>
#include <openssl/evp.h>
#include <fcntl.h>

#define NETDERPER_IP "127.0.0.1"
#define TARGET_PORT 14000
#define LOCAL_ACK_PORT 15001

#define BUFFER_SIZE 1100
#define DATA_SIZE 1024
#define WINDOW_SIZE 8        
#define TIMEOUT_MS 200       
#define MD5_DIGEST_LENGTH 16 

struct __attribute__((packed)) Header {
    uint32_t seq;
    uint32_t crc;
    uint32_t size;
    uint8_t type; 
};

struct WindowSlot {
    uint32_t seq;
    uint8_t type;
    char data[DATA_SIZE];
    uint32_t dataSize;
    bool acked;
    std::chrono::steady_clock::time_point timeSent;
};

uint32_t calculateCRC32(const char *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = p[i];
        for (size_t j = 0; j < 8; j++) {
            uint32_t b = (ch ^ crc) & 1;
            crc >>= 1;
            if (b) crc = crc ^ 0xEDB88320;
            ch >>= 1;
        }
    }
    return ~crc;
}

void sendPacket(int socket, sockaddr_in& destAddr, WindowSlot& slot) {
    char packet[BUFFER_SIZE];
    memset(packet, 0, BUFFER_SIZE);

    Header* head = (Header*)packet;
    head->seq = slot.seq;
    head->size = slot.dataSize;
    head->type = slot.type;
    head->crc = 0;

    if (slot.dataSize > 0) memcpy(packet + sizeof(Header), slot.data, slot.dataSize);
    
    head->crc = calculateCRC32(packet, sizeof(Header) + slot.dataSize);

    sendto(socket, packet, sizeof(Header) + slot.dataSize, 0, (const sockaddr*)&destAddr, sizeof(destAddr));
    
    slot.timeSent = std::chrono::steady_clock::now();
}

int main() {
    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket < 0) return 1;

    int flags = fcntl(clientSocket, F_GETFL, 0);
    fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in myAddr;
    memset(&myAddr, 0, sizeof(myAddr));
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = htons(LOCAL_ACK_PORT);
    myAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(clientSocket, (const sockaddr*)&myAddr, sizeof(myAddr)) < 0) return 1;

    sockaddr_in netDerperAddr;
    memset(&netDerperAddr, 0, sizeof(netDerperAddr));
    netDerperAddr.sin_family = AF_INET;
    netDerperAddr.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, NETDERPER_IP, &netDerperAddr.sin_addr);

    std::string filename = "text.txt";
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return 1;

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    EVP_MD_CTX* md5Context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);

    std::deque<WindowSlot> window;
    uint32_t base = 0;
    uint32_t nextSeqNum = 0;
    bool allDataRead = false;
    bool hashSent = false;

    WindowSlot metaSlot;
    metaSlot.seq = nextSeqNum++;
    metaSlot.type = 3;
    std::string metaStr = filename + "|" + std::to_string(fileSize);
    metaSlot.dataSize = metaStr.length();
    memcpy(metaSlot.data, metaStr.c_str(), metaSlot.dataSize);
    metaSlot.acked = false;
    window.push_back(metaSlot);
    sendPacket(clientSocket, netDerperAddr, window.back());

    char ackBuffer[BUFFER_SIZE];
    sockaddr_in fromAddr;
    socklen_t addrLen = sizeof(fromAddr);

    while (base < nextSeqNum || !hashSent) {
        
        while (window.size() < WINDOW_SIZE && !hashSent) {
            WindowSlot newSlot;
            newSlot.seq = nextSeqNum;
            newSlot.acked = false;

            if (!allDataRead) {
                file.read(newSlot.data, DATA_SIZE);
                newSlot.dataSize = file.gcount();
                if (newSlot.dataSize > 0) {
                    newSlot.type = 0; 
                    EVP_DigestUpdate(md5Context, newSlot.data, newSlot.dataSize);
                }
                
                if (file.eof() || newSlot.dataSize == 0) {
                    allDataRead = true;
                }
                
                if (newSlot.dataSize > 0) {
                    window.push_back(newSlot);
                    sendPacket(clientSocket, netDerperAddr, window.back());
                    nextSeqNum++;
                }
            } else {
                unsigned char hash[MD5_DIGEST_LENGTH];
                unsigned int mdLen;
                EVP_DigestFinal_ex(md5Context, hash, &mdLen);
                
                char hexHash[33];
                for(int i = 0; i < MD5_DIGEST_LENGTH; i++) snprintf(hexHash + (i * 2), 3, "%02x", hash[i]);
                
                newSlot.type = 3; 
                newSlot.dataSize = 32;
                memcpy(newSlot.data, hexHash, 32);
                
                window.push_back(newSlot);
                sendPacket(clientSocket, netDerperAddr, window.back());
                nextSeqNum++;
                hashSent = true;
            }
        }

        int n = recvfrom(clientSocket, ackBuffer, sizeof(Header), 0, (sockaddr*)&fromAddr, &addrLen);
        if (n > 0) {
            Header* ackHead = (Header*)ackBuffer;
            if (ackHead->type == 1) { 
                for (auto &slot : window) {
                    if (slot.seq == ackHead->seq) {
                        slot.acked = true;
                        break;
                    }
                }
                
                while (!window.empty() && window.front().acked) {
                    window.pop_front();
                    base++;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        for (auto &slot : window) {
            if (!slot.acked) {
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - slot.timeSent).count();
                if (duration > TIMEOUT_MS) {
                    sendPacket(clientSocket, netDerperAddr, slot);
                }
            }
        }

        usleep(1000); 
    }

    EVP_MD_CTX_free(md5Context);
    file.close();
    close(clientSocket);
    std::cout << "Done." << std::endl;
    return 0;
}