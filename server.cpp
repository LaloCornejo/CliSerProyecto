#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <thread>
#include <mutex>
#include <errno.h>
#include <chrono>

#define PORT 1234
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// Player structure to store player information
struct Player {
  std::string nombre;
  int techScore;
  int generalScore;
  bool hasCompletedTech;
  bool hasCompletedGeneral;
  int quizTheme;
  int currentQuestion;
};

struct qQuestion {
  std::string question;
  std::string answer;
};

std::vector < qQuestion > techQuestions;
std::vector < qQuestion > generalQuestions;
std::map < int, Player > players;
std::mutex playersMutex;

// Load questions
std::vector < qQuestion > loadQuestions(const std::string & filename) {
  std::vector < qQuestion > questions;
  std::ifstream file(filename);
  std::string line;

  while (std::getline(file, line)) {
    size_t delimiterPos = line.find('|');
    if (delimiterPos != std::string::npos) {
      qQuestion question;
      question.question = line.substr(0, delimiterPos);
      question.answer = line.substr(delimiterPos + 1);
      questions.push_back(question);
    }
  }
  return questions;
}

// Display functions
void moveCursor(int x, int y) {
  printf("\033[%d;%dH", x, y);
}

void printScoreboard() {
  system("clear");
printf("\t\033[1;36m==- Trivia Quiz -==\033[0m\n"
    "++++++++++++++++++++++++++++++++++++++++\n");

  printf("Temi disponibili:\n");
  printf("1- Curiosita sulla tecnologia\n");
printf("2- Cultura Generale\n"
    "+++++++++++++++++++++++++++++++++++++++\n");

  std::lock_guard < std::mutex > lock(playersMutex);

  printf("Partecipanti attivi (%zu):\n", players.size());
  for (const auto & player: players) {
    printf("• %s (Quiz: %d)\n", player.second.nombre.c_str(), player.second.quizTheme);
  }

  printf("\nPunteggi Tecnologia:\n");
  for (const auto & player: players) {
    if (player.second.techScore > 0) {
      printf("%s: %d/5\n", player.second.nombre.c_str(), player.second.techScore);
    }
  }

  printf("\nPunteggi Cultura Generale:\n");
  for (const auto & player: players) {
    if (player.second.generalScore > 0) {
      printf("%s: %d/5\n", player.second.nombre.c_str(), player.second.generalScore);
    }
  }

  printf("\nQuiz Tecnologia completati:\n");
  for (const auto & player: players) {
    if (player.second.hasCompletedTech) {
      printf("• %s\n", player.second.nombre.c_str());
    }
  }

  printf("\nQuiz Cultura Generale completati:\n");
  for (const auto & player: players) {
    if (player.second.hasCompletedGeneral) {
      printf("• %s\n", player.second.nombre.c_str());
    }
  }

  fflush(stdout);
}

void sendScoreboard(int socket) {
  std::stringstream ss;
  std::lock_guard < std::mutex > lock(playersMutex);

  ss << "\nPunteggi Tecnologia:\n";
  for (const auto & player: players) {
    if (player.second.techScore > 0) {
      ss << player.second.nombre << ": " << player.second.techScore << "/5\n";
    }
  }

  ss << "\nPunteggi Cultura Generale:\n";
  for (const auto & player: players) {
    if (player.second.generalScore > 0) {
      ss << player.second.nombre << ": " << player.second.generalScore << "/5\n";
    }
  }

  std::string scoreboardStr = ss.str();
  send(socket, scoreboardStr.c_str(), scoreboardStr.length(), 0);
}

bool checkNickname(const std::string & name) {
  std::lock_guard < std::mutex > lock(playersMutex);
  for (const auto & player: players) {
    if (player.second.nombre == name) {
      return false;
    }
  }
  return true;
}

void handleNewPlayer(int socket) {
  char buffer[BUFFER_SIZE];
  bool validNickname = false;

  while (!validNickname) {
    ssize_t bytes_read = read(socket, buffer, BUFFER_SIZE);
    if (bytes_read <= 0) {
      close(socket);
      return;
    }

    buffer[bytes_read] = '\0';
    std::string nickname(buffer);

    if (checkNickname(nickname)) {
      validNickname = true;

      Player newPlayer;
      newPlayer.nombre = nickname;
      newPlayer.techScore = 0;
      newPlayer.generalScore = 0;
      newPlayer.hasCompletedTech = false;
      newPlayer.hasCompletedGeneral = false;
      newPlayer.quizTheme = 0;
      newPlayer.currentQuestion = 0;

      {
        std::lock_guard < std::mutex > lock(playersMutex);
        players[socket] = newPlayer;
      }

      send(socket, "1", 1, 0);
    } else {
      send(socket, "0", 1, 0);
    }
  }
}

void handleQuiz(int socket) {
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read = read(socket, buffer, BUFFER_SIZE);
  if (bytes_read <= 0) return;

  buffer[bytes_read] = '\0';
  int theme = std::stoi(buffer);

  auto & player = players[socket];
  bool & completed = (theme == 1) ? player.hasCompletedTech : player.hasCompletedGeneral;
  int & score = (theme == 1) ? player.techScore : player.generalScore;
  auto & questions = (theme == 1) ? techQuestions : generalQuestions;

  if (completed) {
    std::string msg = "Hai già completato questo quiz!\n";
    send(socket, msg.c_str(), msg.length(), 0);
    return;
  }

  player.quizTheme = theme;
  player.currentQuestion = 0;

  while (player.currentQuestion < questions.size()) {
    std::string questionMsg = std::string("\n\nQuiz - ") + ((player.quizTheme == 1) ? "Curiosita sulla tecnologia" : "Cultura Generale") + 
        "\n++++++++++++++++++++++++++++++++++++++++\n" + questions[player.currentQuestion].question + "\n";
    send(socket, questionMsg.c_str(), questionMsg.length(), 0);

    bytes_read = read(socket, buffer, BUFFER_SIZE);
    if (bytes_read <= 0) break;

    buffer[bytes_read] = '\0';
    std::string answer(buffer);

    if (answer == "show score") {
      sendScoreboard(socket);
      continue;
    }

    if (answer == "endquiz") {
      std::string msg = "Quiz terminato.\n";
      send(socket, msg.c_str(), msg.length(), 0);
      player.quizTheme = 0;
      return;
    }

    bool correct = (answer == questions[player.currentQuestion].answer);
    std::string resultMsg = correct ? "Risposta corretta!\n" : "Risposta sbagliata.\n";
    send(socket, resultMsg.c_str(), resultMsg.length(), 0);

    if (correct) score++;
    player.currentQuestion++;
  }

  completed = true;
  player.quizTheme = 0;

  std::string finalMsg = "Quiz completato! Punteggio finale: " + std::to_string(score) + "/5\n";
  send(socket, finalMsg.c_str(), finalMsg.length(), 0);
}

void updateScoreboard() {
  while (true) {
    printScoreboard();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

int main() {
  std::string techFile = "tech.txt";
  std::string generalFile = "general.txt";

  try {
    techQuestions = loadQuestions(techFile);
    generalQuestions = loadQuestions(generalFile);
  } catch (const std::exception & e) {
    std::cerr << "Error loading questions: " << e.what() << std::endl;
    return 1;
  }

  std::thread scoreboardThread(updateScoreboard);
  scoreboardThread.detach();

  // Server setup
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("Socket creation failed");
    return 1;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(PORT);

  if (bind(serverSocket, (struct sockaddr * ) & serverAddr, sizeof(serverAddr)) < 0) {
    perror("Bind failed");
    return 1;
  }

  if (listen(serverSocket, MAX_CLIENTS) < 0) {
    perror("Listen failed");
    return 1;
  }

  fd_set readfds;
  std::vector < int > clientSockets;

  while (true) {
    FD_ZERO( & readfds);
    FD_SET(serverSocket, & readfds);
    int maxSd = serverSocket;

    for (int sock: clientSockets) {
      FD_SET(sock, & readfds);
      maxSd = std::max(maxSd, sock);
    }

    if (select(maxSd + 1, & readfds, NULL, NULL, NULL) < 0) {
      perror("Select error");
      continue;
    }

    if (FD_ISSET(serverSocket, & readfds)) {
      struct sockaddr_in clientAddr;
      socklen_t addrLen = sizeof(clientAddr);
      int newSocket = accept(serverSocket, (struct sockaddr * ) & clientAddr, & addrLen);

      if (newSocket >= 0) {
        clientSockets.push_back(newSocket);
      }
    }

    for (auto it = clientSockets.begin(); it != clientSockets.end();) {
      int sock = * it;
      if (FD_ISSET(sock, & readfds)) {
        auto playerIt = players.find(sock);
        if (playerIt == players.end()) {
          handleNewPlayer(sock);
        } else if (playerIt -> second.quizTheme == 0) {
          handleQuiz(sock);
        }
      }

      // Check if socket is still valid
      if (send(sock, "", 0, MSG_NOSIGNAL) < 0) {
        close(sock);
        players.erase(sock);
        it = clientSockets.erase(it);
      } else {
        ++it;
      }
    }
  }

  close(serverSocket);
  return 0;
}
