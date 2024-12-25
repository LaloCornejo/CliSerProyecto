#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <string.h>

#define BUFFER_SIZE 1024

void clearScreen() {
    printf("\033[H\033[J");
}

void frontPage() {
    clearScreen();
    printf("\n==- Trivia Quiz Game -==\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("Menu\n");
    printf("1. Comincia una sessione di trivia\n");
    printf("2. Esci\n");
    printf("La tua scelta: ");
}

void sNickname() {
    clearScreen();
    printf("Trivia Quiz\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("Scegli un nickname (deve essere univoco): ");
}

void sTheme() {
    clearScreen();
    printf("Quiz disponibili\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("1 - Curiosita sulla tecnologia\n");
    printf("2 - Cultura generale\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("La tua scelta: ");
}

void handleQuiz(int sock, const std::string& theme) {
    send(sock, theme.c_str(), theme.length(), 0);
    printf("Sended theme: %s\n", theme.c_str());
    char buffer[BUFFER_SIZE];
    std::string userInput;
    bool quizActive = true;

    while (quizActive) {
        // memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytesRead = read(sock, buffer, BUFFER_SIZE);
        if (bytesRead <= 0) {
            perror("Server disconnected or error reading from server");
            break;
        }

        clearScreen();
        const char* qThemes = (theme == "1") ? "Curiosita sulla tecnologia" : "Cultura generale";
        printf("Quiz - %s\n", qThemes);
        printf("+++++++++++++++++++++++++++++\n");
        printf("\n%s\n", buffer);

        if (strstr(buffer, "Quiz completed!") ||
            strstr(buffer, "Quiz ended") ||
            strstr(buffer, "already completed")) {
            printf("\nPress Enter to continue...\n");
            std::getline(std::cin, userInput);
            quizActive = false;
            continue;
        }

        std::getline(std::cin, userInput);
        send(sock, userInput.c_str(), userInput.length(), 0);

        bool correct;
        read(sock, &correct, sizeof(correct));

        clearScreen();
        if (correct) {
            printf("\nCorrect answer!\n");
        } else {
            printf("\nWrong answer\n");
        }
        std::getline(std::cin, userInput);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int sock = 0;
    struct sockaddr_in serverAddr;
    std::string userInput;
    std::string nickname;
    std::string theme;
    bool running = true;

    while (running) {
        frontPage();
        std::getline(std::cin, userInput);

        if (userInput == "1") {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                perror("Error creating socket");
                return 1;
            }

            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);

            if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
                perror("Invalid address");
                close(sock);
                continue;
            }

            if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                perror("Connection failed");
                close(sock);
                continue;
            }

            send(sock, "1", 1, 0);

            bool nicknameAccepted = false;
            while (!nicknameAccepted) {
                sNickname();
                std::getline(std::cin, nickname);
                
                // Send nickname to server
                send(sock, nickname.c_str(), nickname.length(), 0);
                
                // Wait for server response
                bool response;
                read(sock, &response, sizeof(response));
                
                if (response) {
                    nicknameAccepted = true;
                    clearScreen();
                    printf("Benvenuto %s!\nPress enter to continue... ", nickname.c_str());
                    std::getline(std::cin, userInput);
                } else {
                    printf("Nickname gia in uso, scegline un altro\n");
                    sleep(1);
                }
            }

            sTheme();
            std::getline(std::cin, theme);

            handleQuiz(sock, theme);
            close(sock);
        } else if (userInput == "2") {
            running = false;
        } else {
            printf("\nInvalid option. Press Enter to continue...\n");
            std::getline(std::cin, userInput);
        }
    }

    return 0;
}
