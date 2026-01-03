#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 8888

int main() {
    std::cout << "[DEBUG] Spoustim DIAGNOSTICKY prijimac na portu " << SERVER_PORT << "..." << std::endl;

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        perror("[CHYBA] Socket nelze vytvorit");
        return 1;
    }

    sockaddr_in serverAddr, clientAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (const sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("[CHYBA] Nelze bindovat port (je jiz obsazen?)");
        close(serverSocket);
        return 1;
    }

    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];
    std::ofstream outFile;
    std::string currentFilename = "received_file.bin";
    bool transferActive = false;
    int packetsReceived = 0;

    std::cout << "[DEBUG] Cekam na data..." << std::endl;

    while (true) {
        int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);

        if (bytesReceived > 0) {
            packetsReceived++;
            std::string message(buffer, bytesReceived);
            
            // Vypis informaci o prichozim paketu
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            
            // Jednoducha detekce typu paketu pro vypis
            if (bytesReceived >= 4 && memcmp(buffer, "DATA", 4) == 0) {
                 if (packetsReceived % 50 == 0) { // Vypis jen kazdy 50. datovy paket at nezahltime konzoli
                     std::cout << "[DEBUG] Prijat DATA paket (" << bytesReceived << "B) od " << clientIP << std::endl;
                 }
            } else {
                // Ridici pakety vypisujeme vzdy
                std::cout << "[DEBUG] Prijat RIDICI paket: '" << message << "' od " << clientIP << std::endl;
            }

            // --- Puvodni logika ---
            if (bytesReceived >= 4 && memcmp(buffer, "DATA", 4) == 0) {
                if (transferActive && outFile.is_open()) {
                    uint32_t offset;
                    memcpy(&offset, buffer + 4, sizeof(uint32_t));
                    int dataSize = bytesReceived - 8;
                    if (dataSize > 0) {
                        outFile.seekp(offset, std::ios::beg);
                        outFile.write(buffer + 8, dataSize);
                    }
                } else {
                    // Zde je casty problem: Prisla data, ale transferActive je false
                    if (packetsReceived % 50 == 0)
                        std::cout << "[VAROVANI] Zahazuji data - nebyl prijat prikaz START!" << std::endl;
                }
            }
            else if (message.find("NAME=") == 0) {
                currentFilename = "received_" + message.substr(5);
                std::cout << " -> Nastaveno jmeno souboru: " << currentFilename << std::endl;
            }
            else if (message == "START") {
                if (outFile.is_open()) outFile.close();
                outFile.open(currentFilename, std::ios::binary);
                transferActive = true;
                std::cout << " -> Prenos ZAHAJEN (Soubor otevren)" << std::endl;
            }
            else if (message == "STOP") {
                transferActive = false;
                if (outFile.is_open()) outFile.close();
                std::cout << " -> Prenos UKONCEN (Soubor ulozen)" << std::endl;
                break; 
            }
        }
    }

    close(serverSocket);
    return 0;
}