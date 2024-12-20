#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>

const int PORT = 8080;

class TrivaQuizClient {
  private:
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

  public:
    TrivaQuizClient() {
      if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        exit(EXIT_FAILURE);
      }

      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(PORT);

      if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        exit(EXIT_FAILURE);
      }
    }

  void connectToServer() {
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      std::cerr << "Connection Failed" << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  void run() {
    while(true) {
      printf("==- Trivia Quiz -==\n"
             "1. Start game\n"
             "2. Exit\n");

      int op;
      std::cin >> op;
      switch(op) {
        case 1:
          connectToServer();
          playQuiz();
        case 2:
          close(sock);
          break;
        default:
          std::cerr << "Invalid option" << std::endl;
      }
    }
  }

  void playQuiz() {
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, 1024, 0);
    std::cout << buffer << std::endl;

    std::string nick;
    while(true) {
      std::cout << "Enter your nickname: ";
      std::cin >> nick;

      send(sock, nick.c_str(), nick.size(), 0);

      memset(buffer, 0, sizeof(buffer));
      recv(sock, buffer, 1024, 0);
      std::string response = buffer;
      if(response == "Nickname accepted") {
        break;
      } else {
        std::cerr << "Nickname already taken" << std::endl;
      }
    }

    int theme;
    std::cout << "Choose a theme: ";
    std::cin >> theme;

    char themeChar = std::to_string(theme)[0];
    send(sock, &themeChar, 1, 0);
  }
};

int main (int argc, char *argv[]) {
TrivaQuizClient client;
  client.run();
  return 0;
}
