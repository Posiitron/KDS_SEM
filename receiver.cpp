#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <openssl/evp.h>
#include <iomanip>

#define BUFFER_SIZE 1036 
#define SERVER_PORT 8888
#define MD5_DIGEST_LENGTH 16

struct Header {
    uint32_t seq;
    uint32_t crc;
    uint32_t size;
    uint8_t type;
};

uint32_t calculateCRC32(const char *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        char ch = data[i];
        for (size_t j = 0; j < 8; j++) {
            uint32_t b = (ch ^ crc) & 1;
            crc >>= 1;
            if (b) crc = crc ^ 0xEDB88320;
            ch >>= 1;
        }
    }
    return ~crc;
}

void sendControl(int socket, sockaddr_in& addr, uint32_t seq, uint8_t type) {
    Header head;
    head.seq = seq;
    head.type = type; 
    head.size = 0;
    head.crc = 0; 
    sendto(socket, &head, sizeof(Header), 0, (const sockaddr*)&addr, sizeof(addr));
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        std::cout << "[Error] Could not create socket." << std::endl;
        return 1;
    }

    sockaddr_in serverAddr, clientAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (const sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(serverSocket);
        std::cout << "[Error] Could not bind socket." << std::endl;
        return 1;
    }

    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];
    std::ofstream outFile;
    
    EVP_MD_CTX* md5Context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);
    
    uint32_t expectedSeq = 0;
    bool finished = false;
    std::string finalRemoteHash = "";

    std::cout << "[Info] Waiting for data..." << std::endl;

    while (!finished) {
        int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);

        if (bytesReceived > 0 && (size_t)bytesReceived >= sizeof(Header)) {
            Header* head = (Header*)buffer;
            uint32_t receivedCRC = head->crc;
            head->crc = 0; 
            uint32_t calculatedCRC = calculateCRC32(buffer, bytesReceived);

            if (calculatedCRC != receivedCRC) {
                std::cout << "[Warn] CRC Error on seq " << head->seq << std::endl;
                sendControl(serverSocket, clientAddr, head->seq, 2); 
                continue;
            }

            if (head->seq == expectedSeq) {
                if (head->type == 3) { 
                    std::string payload(buffer + sizeof(Header), head->size);
                    if (head->seq == 0) {
                        size_t delimiter = payload.find('|');
                        std::string filename = "received_" + payload.substr(0, delimiter);
                        outFile.open(filename, std::ios::binary);
                        std::cout << "[Info] Receiving: " << filename << std::endl;
                    } else {
                        finalRemoteHash = payload;
                        finished = true;
                    }
                } else if (head->type == 0) { 
                     if (outFile.is_open()) {
                        outFile.write(buffer + sizeof(Header), head->size);
                        EVP_DigestUpdate(md5Context, buffer + sizeof(Header), head->size);
                     }
                }
                
                sendControl(serverSocket, clientAddr, head->seq, 1); 
                expectedSeq++;
            } else if (head->seq < expectedSeq) {
                sendControl(serverSocket, clientAddr, head->seq, 1); 
            }
        }
    }

    if (outFile.is_open()) outFile.close();
    close(serverSocket);

    unsigned char localHashBytes[MD5_DIGEST_LENGTH];
    unsigned int mdLen;
    EVP_DigestFinal_ex(md5Context, localHashBytes, &mdLen);
    EVP_MD_CTX_free(md5Context);
    
    char localHashHex[33];
    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) snprintf(localHashHex + (i * 2), 3, "%02x", localHashBytes[i]);

    std::cout << "[Info] Transfer complete." << std::endl;
    std::cout << "[Info] Remote MD5: " << finalRemoteHash << std::endl;
    std::cout << "[Info] Local MD5:  " << localHashHex << std::endl;

    if (finalRemoteHash == std::string(localHashHex)) {
        std::cout << "[Success] File integrity verified." << std::endl;
    } else {
        std::cout << "[Error] Hash mismatch!" << std::endl;
    }

    return 0;
}