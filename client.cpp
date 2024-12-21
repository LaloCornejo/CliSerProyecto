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

void handleQuiz(int sock, std::string theme) {
  char buffer[BUFFER_SIZE];
  std::string userInput;
  bool quizActive = true;

  while (quizActive) {
    memset(buffer, 0, BUFFER_SIZE);
    int bytesRead = read(sock, buffer, BUFFER_SIZE);
    if (bytesRead <= 0) {
      perror("Server disconnected or error reading from server");
      break;
    }

    clearScreen();
    std::string qThemes = (theme == "1") ? "Curiosita sulla tecnologia" : "Cultura generale";
    printf("Quiz - %s\n", qThemes.c_str());
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
    send(sock, userInput.c_str(), userInput.size(), 0);
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
    sNickname();
    std::getline(std::cin, nickname);
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

      send(sock, nickname.c_str(), nickname.size(), 0);

      sTheme();
      std::getline(std::cin, theme);
      send(sock, theme.c_str(), theme.size(), 0);

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
