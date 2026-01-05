#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <openssl/evp.h>
#include <iomanip>
#include <map>
#include <vector>

#define LISTEN_PORT 15000
#define ACK_TARGET_PORT 14001
#define BUFFER_SIZE 1100 
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

void sendAck(int socket, sockaddr_in& senderAddr, uint32_t seq) {
    Header head;
    head.seq = seq;
    head.type = 1; // ACK
    head.size = 0;
    head.crc = 0; 
    
    sockaddr_in responseAddr = senderAddr;
    responseAddr.sin_port = htons(ACK_TARGET_PORT);

    sendto(socket, &head, sizeof(Header), 0, (const sockaddr*)&responseAddr, sizeof(responseAddr));
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) return 1;

    sockaddr_in serverAddr, clientAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (const sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(serverSocket);
        return 1;
    }

    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];
    std::ofstream outFile;
    
    EVP_MD_CTX* md5Context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(md5Context, EVP_md5(), NULL);
    
    // Buffer for out-of-order packets
    std::map<uint32_t, std::vector<char>> packetBuffer;
    
    uint32_t expectedSeq = 0;
    bool finished = false;
    std::string finalRemoteHash = "";

    std::cout << "Selective Repeat Receiver Started..." << std::endl;

    while (!finished) {
        int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);

        if (bytesReceived > 0 && (size_t)bytesReceived >= sizeof(Header)) {
            Header* head = (Header*)buffer;
            
            // 1. Verify CRC
            uint32_t receivedCRC = head->crc;
            head->crc = 0; 
            uint32_t calculatedCRC = calculateCRC32(buffer, bytesReceived);

            if (calculatedCRC != receivedCRC) {
                // Silent drop on CRC error (Sender will timeout)
                std::cout << "[CRC Error] Dropping seq " << head->seq << std::endl;
                continue;
            }

            // 2. Send ACK (Always send ACK for received packet)
            sendAck(serverSocket, clientAddr, head->seq);

            // 3. Process Packet
            if (head->seq == expectedSeq) {
                // In-order packet
                if (head->type == 3 && head->seq == 0) {
                    // Start Metadata
                    std::string payload(buffer + sizeof(Header), head->size);
                    size_t delimiter = payload.find('|');
                    std::string filename = "received_" + payload.substr(0, delimiter);
                    outFile.open(filename, std::ios::binary);
                    std::cout << "Receiving: " << filename << std::endl;
                    expectedSeq++;
                } 
                else if (head->type == 0) {
                    // Data
                    if (outFile.is_open()) {
                        outFile.write(buffer + sizeof(Header), head->size);
                        EVP_DigestUpdate(md5Context, buffer + sizeof(Header), head->size);
                    }
                    expectedSeq++;
                }
                else if (head->type == 3 && head->seq > 0) {
                    // End Metadata (Hash)
                    std::string payload(buffer + sizeof(Header), head->size);
                    finalRemoteHash = payload;
                    finished = true;
                    expectedSeq++;
                }

                // 4. Check Buffer for subsequent packets
                while (packetBuffer.count(expectedSeq)) {
                    std::vector<char> &savedData = packetBuffer[expectedSeq];
                    Header* savedHead = (Header*)savedData.data();
                    
                    if (savedHead->type == 0) {
                        outFile.write(savedData.data() + sizeof(Header), savedHead->size);
                        EVP_DigestUpdate(md5Context, savedData.data() + sizeof(Header), savedHead->size);
                    } else if (savedHead->type == 3) {
                        finalRemoteHash = std::string(savedData.data() + sizeof(Header), savedHead->size);
                        finished = true;
                    }
                    
                    packetBuffer.erase(expectedSeq);
                    expectedSeq++;
                }

            } else if (head->seq > expectedSeq) {
                // Out-of-order packet (Buffer it)
                if (packetBuffer.find(head->seq) == packetBuffer.end()) {
                    std::vector<char> savedPacket(buffer, buffer + bytesReceived);
                    packetBuffer[head->seq] = savedPacket;
                }
            }
            // If seq < expectedSeq, we just sent ACK (duplicate), doing nothing else.
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

    std::cout << "Remote MD5: " << finalRemoteHash << std::endl;
    std::cout << "Local MD5:  " << localHashHex << std::endl;

    if (finalRemoteHash == std::string(localHashHex)) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "HASH MISMATCH" << std::endl;
    }

    return 0;
}