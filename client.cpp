#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <string.h>

#define BUFFER_SIZE 1024

void clearScreen() {
    printf("\033[H\033[J");
}

void showMenu() {
  clearScreen();
  printf("\n==- Trivia Quiz Game -==\n");
  printf("1. Start Game\n");
  printf("2. Exit\n");
  printf("Choose an option: ");
}

void handleQuiz(int sock) {
  std::cerr << "handleQuiz\n";

  char buffer[BUFFER_SIZE];
  std::string userInput;
  bool quizActive = true;

  while(quizActive) {
    memset(buffer, 0, BUFFER_SIZE);
    int bytesRead = read(sock, buffer, BUFFER_SIZE);

    if(bytesRead < 0) {
      perror("\nServer disconnected\n");
      break;
    }

    printf("\n%s\n", buffer);

    if(strstr(buffer, "Quiz completed!") ||
        strstr(buffer, "Quiz ended") ||
        strstr(buffer, "already completed")) {
      printf("\nPress any key to continue...\n");
      std::getline(std::cin, userInput);
      quizActive = false;
      continue;
    }
  }
}

int main (int argc, char *argv[]) {
  if(argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);
  int sock = 0;
  struct sockaddr_in serverAddr;
  std::string userInput;
  bool running = true;

  while(running) {
    showMenu();
    std::getline(std::cin, userInput);

    if(userInput == "1") {
      sock = socket(AF_INET, SOCK_STREAM, 0);
      if(sock < 0) {
        perror("Error creating socket");
        return 1;
      }

      serverAddr.sin_family = AF_INET;
      serverAddr.sin_port = htons(port);
      
      if(inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        continue;
      }

      if(connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        close(sock);
        continue;
      }

      clearScreen();
      printf("\nConnected to server\n");
      handleQuiz(sock);
      // close(sock);
    } else if(userInput == "2") {
      running = false;
    } else {
      printf("\nInvalid option. Press Enter to continue...\n");
      std::getline(std::cin, userInput);
    }
  }

  return 0;
}
