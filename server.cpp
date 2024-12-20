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
#define MAX_CLIENTS 100

struct Player {
  std::string nombre;
  int techScore;
  int generalScore;
  int quizTheme;
  bool hasCompletedTech;
  bool hasCompletedGeneral;
};

struct qQuestion {
  std::string question;
  std::string answer;
};

std::vector<qQuestion> techQuestions;
std::vector<qQuestion> generalQuestions;
std::map<int, Player> players;

std::vector<qQuestion> loadQuestions(std::string& filename) {
  std::vector<qQuestion> questions;
  std::ifstream file(filename);
  std::string line;

  while (std::getline(file, line)) {
    size_t delimiterPos = line.find('|');
    if( delimiterPos != std::string::npos ) {
      qQuestion question;
      question.question = line.substr(0, delimiterPos);
      question.answer = line.substr(delimiterPos + 1);
      questions.push_back(question);
    }
  }

  return questions;
}

bool uniqueName(std::string name) {
  for (auto& player : players) {
    if (player.second.nombre == name) {
      return false;
    }
  }
  return true;
}

void moveCursor(int x, int y) {
    printf("\033[%d;%dH", y, x);
}

void printScoreboard() {
    moveCursor(1, 1);
    printf("\n\nTrivia Quiz\n"
           "++++++++\n"
           "Temi:\n"
           "1- Curiosita sulla tecnologia\n"
           "2- Cultura Generale\n"
           "+++++++++++++++-\n"
           "Partecipanti (%zu)\n", players.size());

    moveCursor(1, 6);
    for (const auto& player : players) {
        printf("%s\n", player.second.nombre.c_str());
    }

    moveCursor(1, 6 + players.size() + 2);
    printf("Puntaggio tema 1\n");
    for (const auto& player : players) {
        printf("%s: %d\n", player.second.nombre.c_str(), player.second.techScore);
    }

    moveCursor(1, 6 + players.size() + 4 + players.size());
    printf("Puntaggio tema 2\n");
    for (const auto& player : players) {
        printf("%s: %d\n", player.second.nombre.c_str(), player.second.generalScore);
    }

    moveCursor(1, 6 + players.size() + 6 + players.size() * 2);
    printf("Quiz Tema 1 completato\n");
    for (const auto& player : players) {
        if (player.second.hasCompletedTech && player.second.quizTheme == 1) {
            printf("%s\n", player.second.nombre.c_str());
        }
    }

    moveCursor(1, 6 + players.size() + 8 + players.size() * 2); 
    printf("Quiz Tema 2 completato\n");
    for (const auto& player : players) {
        if (player.second.hasCompletedGeneral && player.second.quizTheme == 2) {
            printf("%s\n", player.second.nombre.c_str());
        }
    }

    moveCursor(1, 6 + players.size() + 10 + players.size() * 2); 
  printf("+++++++++++++++\n");
}

void updateScoreboard() {
    printScoreboard();
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

void handleNewPlayer(int socket) {
  char buffer[BUFFER_SIZE];
  std::string nickname;
  bool validNickname = false;
  
  while (!validNickname) {
    send(socket, "Enter nickname: ", 15, 0);
    int bytes = read(socket, buffer, BUFFER_SIZE);
    buffer[bytes] = '\0';
    nickname = std::string(buffer);
    nickname.erase(std::remove(nickname.begin(), nickname.end(), '\n'), nickname.end());
    
    if (uniqueName(nickname)) {
      validNickname = true;
      Player newPlayer;
      newPlayer.nombre = nickname;
      newPlayer.techScore = 0;
      newPlayer.generalScore = 0;
      newPlayer.hasCompletedTech = false;
      newPlayer.hasCompletedGeneral = false;
      newPlayer.quizTheme = 0;
      players[socket] = newPlayer;
      
      std::string welcome = "Welcome " + nickname + "!\n1- Technology Quiz\n2- General Knowledge Quiz\nChoose quiz (1/2): ";
      send(socket, welcome.c_str(), welcome.length(), 0);
    } else {
      send(socket, "Nickname already taken. Try again.\n", 32, 0);
    }
  }
}

void handleQuiz(int socket, int theme) {
  Player& player = players[socket];
  std::vector<qQuestion>& questions = (theme == 1) ? techQuestions : generalQuestions;
  int& score = (theme == 1) ? player.techScore : player.generalScore;
  bool& completed = (theme == 1) ? player.hasCompletedTech : player.hasCompletedGeneral;
  
  if (completed) {
    send(socket, "You have already completed this quiz.\n", 36, 0);
    return;
  }

  player.quizTheme = theme;
  char buffer[BUFFER_SIZE];
  
  for (int i = 0; i < questions.size(); i++) {
    std::string questionMsg = "Question " + std::to_string(i+1) + ": " + questions[i].question + "\n";
    send(socket, questionMsg.c_str(), questionMsg.length(), 0);
    
    int bytes = read(socket, buffer, BUFFER_SIZE);
    buffer[bytes] = '\0';
    std::string answer(buffer);
    answer.erase(std::remove(answer.begin(), answer.end(), '\n'), answer.end());
    
    if (answer == "show score") {
      std::string scoreBoard = "Current Scores:\nTechnology Quiz:\n";
      for (const auto& p : players) {
        scoreBoard += p.second.nombre + ": " + std::to_string(p.second.techScore) + "\n";
      }
      scoreBoard += "\nGeneral Knowledge Quiz:\n";
      for (const auto& p : players) {
        scoreBoard += p.second.nombre + ": " + std::to_string(p.second.generalScore) + "\n";
      }
      send(socket, scoreBoard.c_str(), scoreBoard.length(), 0);
      i--;
      continue;
    }
    
    if (answer == "endquiz") {
      players.erase(socket);
      send(socket, "Quiz ended. Goodbye!\n", 20, 0);
      return;
    }
    
    if (answer == questions[i].answer) {
      score++;
      send(socket, "Correct answer!\n", 15, 0);
    } else {
      send(socket, "Wrong answer!\n", 13, 0);
    }
  }
  
  completed = true;
  std::string finalMsg = "Quiz completed! Your final score: " + std::to_string(score) + "\n";
  send(socket, finalMsg.c_str(), finalMsg.length(), 0);
}

void run(int sd) {
  char buffer[BUFFER_SIZE];
  int bytes = read(sd, buffer, BUFFER_SIZE);
  buffer[bytes] = '\0';
  std::string command(buffer);
  command.erase(std::remove(command.begin(), command.end(), '\n'), command.end());
  
  auto it = players.find(sd);
  if (it == players.end()) {
    handleNewPlayer(sd);
  } else {
    if (it->second.quizTheme == 0) {
      int theme = std::stoi(command);
      if (theme == 1 || theme == 2) {
        handleQuiz(sd, theme);
      } else {
        send(sd, "Invalid theme. Choose 1 or 2.\n", 28, 0);
      }
    }
  }
  
  updateScoreboard();
}


int main (int argc, char *argv[]) {

  updateScoreboard();
  std::string techFile = "tech.txt";
  std::string generalFile = "general.txt";
  techQuestions = loadQuestions(techFile);
  generalQuestions = loadQuestions(generalFile);

int serverSocket, maxSd, sd, activity, newSocket;
  struct sockaddr_in serverAddr, clientAddr;
  int playerSockets[MAX_CLIENTS] = {0};
  socklen_t addrLen;
  fd_set readfds;
  char buffer[BUFFER_SIZE];

  serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket == -1) {
    perror("Eror Socket creation");
    exit(EXIT_FAILURE);
  }

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(PORT);

  if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    perror("Error binding");
    close(serverSocket);
    exit(EXIT_FAILURE);
  }

  if (listen(serverSocket, 3) < 0) {
    perror("Error listening");
    close(serverSocket);
    exit(EXIT_FAILURE);
  }

  addrLen = sizeof(clientAddr);
  printf("Listening port: %d\n", PORT);

  while(true) {
    FD_ZERO(&readfds);
    FD_SET(serverSocket, &readfds);
    maxSd = serverSocket;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      sd = playerSockets[i];
      if (sd > 0) {
        FD_SET(sd, &readfds);
      }
      if (sd > maxSd) {
        maxSd = sd;
      }
    }

    activity = select(maxSd + 1, &readfds, NULL, NULL, NULL);
    if ((activity < 0) && (errno != EINTR)) {
      perror("Error select");
    }

    if (FD_ISSET(serverSocket, &readfds)) {
      if ((newSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &addrLen)) < 0) {
        perror("Error accept");
        exit(EXIT_FAILURE);
      }

      printf("New connection, socket fd is %d, ip is: %s, port: %d\n", newSocket, inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (playerSockets[i] == 0) {
          playerSockets[i] = newSocket;
          printf("Adding to list of sockets as %d\n", i);
          break;
        }
      }
    }
    for(int i = 0; i < MAX_CLIENTS; i++ ){
    sd = playerSockets[i];
      if( FD_ISSET(sd, &readfds)) {
        int valRead = read(sd, buffer, BUFFER_SIZE);
        if( valRead == 0 ) {
          getpeername(sd, (struct sockaddr*)&clientAddr, &addrLen);
          printf("Client disconnected, ip %s, port %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
          close(sd);
          playerSockets[i] = 0;
        } else {
        run(sd);
        }
      }
    }
  }
  close(serverSocket);
  return 0;
}
