#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <vector>
#include <openssl/evp.h>

#define NETDERPER_IP "127.0.0.1"
#define TARGET_PORT 14000
#define LOCAL_ACK_PORT 15001
#define BUFFER_SIZE 1100
#define DATA_SIZE 1024
#define TIMEOUT_US 100000 
#define MD5_DIGEST_LENGTH 16

struct __attribute__((packed)) Header {
    uint32_t seq;
    uint32_t crc;
    uint32_t size;
    uint8_t type; 
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

void sendPacketReliable(int socket, sockaddr_in& destAddr, uint32_t seq, uint8_t type, const char* data, uint32_t size) {
    char packet[BUFFER_SIZE];
    memset(packet, 0, BUFFER_SIZE);

    Header* head = (Header*)packet;
    head->seq = seq;
    head->size = size;
    head->type = type;
    head->crc = 0;

    if (size > 0) memcpy(packet + sizeof(Header), data, size);
    
    head->crc = calculateCRC32(packet, sizeof(Header) + size);

    bool acknowledged = false;
    socklen_t addrLen = sizeof(destAddr);
    char ackBuffer[BUFFER_SIZE]; 
    sockaddr_in fromAddr;

    while (!acknowledged) {
        sendto(socket, packet, sizeof(Header) + size, 0, (const sockaddr*)&destAddr, sizeof(destAddr));

        int n = recvfrom(socket, ackBuffer, sizeof(Header), 0, (sockaddr*)&fromAddr, &addrLen);
        if (n > 0) {
            Header* ackHead = (Header*)ackBuffer;
            if (ackHead->type == 1 && ackHead->seq == seq) {
                acknowledged = true;
            }
        } 
    }
}

int main() {
    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket < 0) return 1;

    sockaddr_in myAddr;
    memset(&myAddr, 0, sizeof(myAddr));
    myAddr.sin_family = AF_INET;
    myAddr.sin_port = htons(LOCAL_ACK_PORT);
    myAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(clientSocket, (const sockaddr*)&myAddr, sizeof(myAddr)) < 0) return 1;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_US;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in netDerperAddr;
    memset(&netDerperAddr, 0, sizeof(netDerperAddr));
    netDerperAddr.sin_family = AF_INET;
    netDerperAddr.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, NETDERPER_IP, &netDerperAddr.sin_addr);

    std::string filename = "text.txt";
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        close(clientSocket);
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    EVP_MD_CTX* md5Context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);

    uint32_t seqNum = 0;

    std::string metaData = filename + "|" + std::to_string(fileSize);
    sendPacketReliable(clientSocket, netDerperAddr, seqNum++, 3, metaData.c_str(), metaData.length());

    char buffer[DATA_SIZE];
    while (file.good()) {
        file.read(buffer, DATA_SIZE);
        int bytesRead = (int)file.gcount();
        if (bytesRead > 0) {
            EVP_DigestUpdate(md5Context, buffer, bytesRead);
            sendPacketReliable(clientSocket, netDerperAddr, seqNum++, 0, buffer, bytesRead);
        }
    }

    unsigned char hash[MD5_DIGEST_LENGTH];
    unsigned int mdLen;
    EVP_DigestFinal_ex(md5Context, hash, &mdLen);
    EVP_MD_CTX_free(md5Context);
    
    char hexHash[33];
    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) snprintf(hexHash + (i * 2), 3, "%02x", hash[i]);

    sendPacketReliable(clientSocket, netDerperAddr, seqNum++, 3, hexHash, 32);

    file.close();
    close(clientSocket);

    std::cout << "Done." << std::endl;
    return 0;
}