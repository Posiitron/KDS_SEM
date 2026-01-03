#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <vector>

#define BUFFER_SIZE 1024
#define SERVER_IP "192.168.39.125"
#define SERVER_PORT 8888

int main() {
    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket < 0) {
        std::cout << "[Error] Could not create socket." << std::endl;
        return 1;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    std::string filename = "text.txt";
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        close(clientSocket);
        std::cout << "[Error] Could not open file: " << filename << std::endl;
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string nameMsg = "NAME=" + filename;
    sendto(clientSocket, nameMsg.c_str(), nameMsg.length(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));
    usleep(10000);

    std::string sizeMsg = "SIZE=" + std::to_string(fileSize);
    sendto(clientSocket, sizeMsg.c_str(), sizeMsg.length(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));
    usleep(10000);

    std::string startMsg = "START";
    sendto(clientSocket, startMsg.c_str(), startMsg.length(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));
    usleep(10000);

    char buffer[BUFFER_SIZE];
    uint32_t offset = 0;

    while (file.good() && offset < fileSize) {
        file.read(buffer + 8, BUFFER_SIZE - 8);
        int bytesRead = (int)file.gcount();

        if (bytesRead > 0) {
            memcpy(buffer, "DATA", 4);
            memcpy(buffer + 4, &offset, sizeof(uint32_t));

            sendto(clientSocket, buffer, bytesRead + 8, 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));

            offset += bytesRead;
            usleep(5000);
        }
    }

    std::string stopMsg = "STOP";
    sendto(clientSocket, stopMsg.c_str(), stopMsg.length(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr));

    file.close();
    close(clientSocket);

    std::cout << "[Info] File succesfully sent." << std::endl;
    return 0;
}