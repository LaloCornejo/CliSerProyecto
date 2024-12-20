#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <cstring>

const int PORT = 8080;
const int MAX_PLAYERS = 10;

struct Player {
  std::string nick;
  int techScore;
  int generalScore;
  int quizTheme;
  int hasCompletedQuiz;
};

struct QuizQuestion {
  std::string question;
  std::string answer;
};

std::vector<QuizQuestion> techQuestions;
std::vector<QuizQuestion> generalQuestions;
std::map<int, Player> players;

std::vector<QuizQuestion> loadQuestion(const std::string& filename) {
  std::vector<QuizQuestion> questions;
  std::ifstream file(filename);
  std::string line;

  while(std::getline(file, line)) {
    size_t delimiterPos = line.find("|");
    if(delimiterPos != std::string::npos) {
    QuizQuestion q;
    q.question = line.substr(0, delimiterPos);
    q.answer = line.substr(delimiterPos + 1);
    questions.push_back(q);
    }
  }

  return questions;
}

bool nicknameUnique(const std::string& nick) {
  for(const auto& player : players) {
    if(player.second.nick == nick) {
      return false;
    }
  }
  return true;
}

void printScoreboard() {
printf("Trivia Quiz\n"
        "++++++++\n"
        "Temi:\n"
        "1- Curiosita sulla tecnologia\n"
        "2- Cultura Generale\n"
        "+++++++++++++++-\n"
        "Partecipanti (%zu)\n", players.size());
  for(const auto& player : players) {
    printf("%s\n", player.second.nick.c_str());
  }
  printf("Puntaggio tema 1\n");
  for(const auto& player : players) {
    printf("%s: %d\n", player.second.nick.c_str(), player.second.techScore);
  }
  printf("Puntaggio tema 2\n");
  for(const auto& player : players) {
    printf("%s: %d\n", player.second.nick.c_str(), player.second.generalScore);
  }
  printf("Quiz Tema 1 completato\n");
  for(const auto& player : players) {
    if(player.second.hasCompletedQuiz && player.second.quizTheme == 1) {
      printf("%s\n", player.second.nick.c_str());
    }
  }
  printf("Quiz Tema 2 completato\n");
  for(const auto& player : players) {
    if(player.second.hasCompletedQuiz && player.second.quizTheme == 2) {
      printf("%s\n", player.second.nick.c_str());
    }
  }
  printf("+++++++++++++++\n");
}


void handleClient(int clientSocket) {
  char buffer[1024] = {0};
  ssize_t valread;

  std::string themes = "1. Tech\n2. General\n";
  send(clientSocket, themes.c_str(), themes.size(), 0);

  std::string nick;
  while(true) {
    valread = recv(clientSocket, buffer, 1024, 0);
    if(valread<=0) { return; }

    nick = std::string(buffer, valread);
    nick.erase(nick.find_last_not_of(" \n\r\t") + 1);

    if(nicknameUnique(nick)) {
      std::string success = "Nickname accepted\n";
      send(clientSocket, success.c_str(), success.size(), 0);
      break;
    } else {
      std::string error = "Nickname already taken\n";
      send(clientSocket, error.c_str(), error.size(), 0);
    }
    memset(buffer, 0, sizeof(buffer));
  }

  int quizTheme;
  valread = recv(clientSocket, buffer, 1024, 0);
  if(valread<=0) { return; }

  quizTheme = buffer[0] - '0';

  int playerID = clientSocket;
  players[playerID] = {
    nick, 
    0,
    0,
    quizTheme,
    false
  };
}

int main() {
techQuestions = loadQuestion("tech.txt");
generalQuestions = loadQuestion("general.txt");

int serverSocket, newSocket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    std::cerr << "Socket failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
    std::cerr << "Bind failed" << std::endl;
    exit(EXIT_FAILURE);
  }

if (listen(serverSocket, MAX_PLAYERS) < 0) {
    std::cerr << "Listen failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "Server is running on port " << PORT << std::endl;

while(true) {
pid_t scoreboardPid = fork();
if(scoreboardPid == 0) {
    printScoreboard();
    exit(0);
}
    if ((newSocket = accept(serverSocket, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
      std::cerr << "Accept failed" << std::endl;
      exit(EXIT_FAILURE);
    }

    std::cout << "New connection" << std::endl;

    pid_t pid = fork();
    if(pid == 0) {
      close(serverSocket);
      handleClient(newSocket);
      close(newSocket);
      exit(0);
    } else {
      close(newSocket);
    }
  }
  return 0;
}
