#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <cstring>

#define BUFFER_SIZE 1024
#define SERVER_PORT 8888

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
    std::string currentFilename = "received_file.bin";
    bool transferActive = false;

    while (true) {
        int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);

        if (bytesReceived > 0) {
            std::string message(buffer, bytesReceived);

            if (bytesReceived >= 4 && memcmp(buffer, "DATA", 4) == 0) {
                if (transferActive && outFile.is_open()) {
                    uint32_t offset;
                    memcpy(&offset, buffer + 4, sizeof(uint32_t));

                    int dataSize = bytesReceived - 8;
                    if (dataSize > 0) {
                        outFile.seekp(offset, std::ios::beg);
                        outFile.write(buffer + 8, dataSize);
                    }
                }
            }
            else if (message.find("NAME=") == 0) {
                currentFilename = "received_" + message.substr(5);
            }
            else if (message.find("SIZE=") == 0) {
            }
            else if (message == "START") {
                if (outFile.is_open()) outFile.close();
                outFile.open(currentFilename, std::ios::binary);
                transferActive = true;
            }
            else if (message == "STOP") {
                transferActive = false;
                if (outFile.is_open()) outFile.close();
                break;
            }
        }
    }

    close(serverSocket);

    std::cout << "[Info] File successfully received." << std::endl;
    return 0;
}