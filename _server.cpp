#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define PORT 1234
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

std::ofstream logFile("server_log.txt", std::ios::app);
std::mutex playersMutex;
volatile sig_atomic_t running = 1;

void sigintHandler(int sig) {
  std::string separator(40, '-');
  std::lock_guard<std::mutex> lock(playersMutex);
  logFile << separator << std::endl;
  logFile << "SERVER TERMINATED BY USER" << std::endl;
  logFile << separator << std::endl;
  logFile.flush();
  running = 0;
}
void logMessage(const std::string &message) {
  std::lock_guard<std::mutex> lock(playersMutex);
  logFile << "["
          << std::chrono::system_clock::to_time_t(
                 std::chrono::system_clock::now())
          << "] " << message << std::endl;
}

bool secureSend(int sock, const std::string &message) {
  size_t total_sent = 0;
  size_t len = message.length();

  while (total_sent < len) {
    ssize_t bytes_sent =
        send(sock, message.c_str() + total_sent, len - total_sent, 0);
    if (bytes_sent < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        continue;
      }
      logMessage("Error in send operation to socket " + std::to_string(sock));
      return false;
    }
    total_sent += bytes_sent;
  }
  logMessage("Successfully sent total " + std::to_string(total_sent) +
             " bytes to socket " + std::to_string(sock));
  return true;
}

std::pair<bool, std::string> secureRead(int sock, size_t maxSize) {
  std::string result;
  std::vector<char> buffer(maxSize);

  while (true) {
    ssize_t bytes_read = read(sock, buffer.data(), maxSize - result.length());

    if (bytes_read < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        if (result.empty()) {
          continue;
        }
        break;
      }
      logMessage("Error in read operation from socket " + std::to_string(sock));
      return {false, ""};
    } else if (bytes_read == 0) {
      logMessage("Connection closed by peer on socket " + std::to_string(sock));
      if (result.empty()) {
        return {false, ""};
      }
      break;
    }

    logMessage("Received " + std::to_string(bytes_read) +
               " bytes from socket " + std::to_string(sock));
    result.append(buffer.data(), bytes_read);
    if (bytes_read < static_cast<ssize_t>(maxSize) ||
        result.length() >= maxSize) {
      break;
    }
  }

  return {true, result};
}

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

std::vector<qQuestion> techQuestions;
std::vector<qQuestion> generalQuestions;
std::map<int, Player> players;

std::vector<qQuestion> loadQuestions(const std::string &filename) {
  std::vector<qQuestion> questions;
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

void moveCursor(int x, int y) { printf("\033[%d;%dH", x, y); }

void printScoreboard() {
  std::stringstream ss;

  ss << "\033[2J\033[H";

  ss << "\t\033[1;36m    ==- Trivia Quiz -==\033[0m\n"
     << "++++++++++++++++++++++++++++++++++++++++\n"
     << "Temi disponibili:\n"
     << "1- Curiosita sulla tecnologia\n"
     << "2- Cultura Generale\n"
     << "+++++++++++++++++++++++++++++++++++++++\n";

  std::lock_guard<std::mutex> lock(playersMutex);

  ss << "Partecipanti attivi (" << players.size() << "):\n";
  for (const auto &player : players) {
    ss << "• " << player.second.nombre << " (Quiz: " << player.second.quizTheme
       << ")\n";
  }

std::vector<std::pair<std::string, int>> techScores;
for (const auto &player : players) {
    if (player.second.techScore > 0) {
        techScores.push_back({player.second.nombre, player.second.techScore});
    }
}
std::sort(techScores.begin(), techScores.end(),
    [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) { return a.second > b.second; });

ss << "\nPunteggi Tecnologia:\n";
for (const auto &score : techScores) {
    ss << score.first << ": " << score.second << "/" << players.size() << "\n";
}

std::vector<std::pair<std::string, int>> generalScores;
for (const auto &player : players) {
    if (player.second.generalScore > 0) {
        generalScores.push_back({player.second.nombre, player.second.generalScore});
    }
}
std::sort(generalScores.begin(), generalScores.end(),
    [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) { return a.second > b.second; });

ss << "\nPunteggi Cultura Generale:\n";
for (const auto &score : generalScores) {
    ss << score.first << ": " << score.second << "/" << players.size() << "\n";
}

  ss << "\nQuiz Tecnologia completati:\n";
  for (const auto &player : players) {
    if (player.second.hasCompletedTech) {
      ss << "x " << player.second.nombre << "\n";
    }
  }

  ss << "\nQuiz Cultura Generale completati:\n";
  for (const auto &player : players) {
    if (player.second.hasCompletedGeneral) {
      ss << "x " << player.second.nombre << "\n";
    }
  }

  printf("%s", ss.str().c_str());
  fflush(stdout);
}

void sendScoreboard(int socket) {
  std::stringstream ss;
  std::lock_guard<std::mutex> lock(playersMutex);

std::vector<std::pair<std::string, int>> techScores;
for (const auto &player : players) {
    if (player.second.techScore > 0) {
        techScores.push_back({player.second.nombre, player.second.techScore});
    }
}
std::sort(techScores.begin(), techScores.end(),
    [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) { return a.second > b.second; });

ss << "\nPunteggi Tecnologia:\n";
for (const auto &score : techScores) {
    ss << score.first << ": " << score.second << "/" << players.size() << "\n";
}

std::vector<std::pair<std::string, int>> generalScores;
for (const auto &player : players) {
    if (player.second.generalScore > 0) {
        generalScores.push_back({player.second.nombre, player.second.generalScore});
    }
}
std::sort(generalScores.begin(), generalScores.end(),
    [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) { return a.second > b.second; });

ss << "\nPunteggi Cultura Generale:\n";
for (const auto &score : generalScores) {
    ss << score.first << ": " << score.second << "/" << players.size() << "\n";
}

  std::string scoreboardStr = ss.str();
  logMessage("Sending scoreboard to player " + std::to_string(socket));
  if (!secureSend(socket, scoreboardStr)) {
    logMessage("Error sending scoreboard to player " + std::to_string(socket));
  }
}

bool checkNickname(const std::string &name) {
  std::lock_guard<std::mutex> lock(playersMutex);
  for (const auto &player : players) {
    if (player.second.nombre == name) {
      return false;
    }
  }
  return true;
}

void handleNewPlayer(int socket) {
  bool validNickname = false;

  while (!validNickname) {
    std::pair<bool, std::string> result = secureRead(socket, BUFFER_SIZE);
    bool success;
    std::string nickname;
    std::tie(success, nickname) = result;
    if (!success) {
      close(socket);
      logMessage("Error reading nickname from new player.");
      return;
    }

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
        std::lock_guard<std::mutex> lock(playersMutex);
        players[socket] = newPlayer;
      }

      if (!secureSend(socket, "1")) {
        logMessage("Error sending confirmation to new player.");
      }
      logMessage("New player joined: " + nickname);
    } else {
      if (!secureSend(socket, "0")) {
        logMessage("Error sending nickname in use message to new player.");
      }
      logMessage("Nickname already in use: " + nickname);
    }
  }
}

void handleQuiz(int socket) {
std::pair<bool, std::string> result = secureRead(socket, BUFFER_SIZE);
bool success;
std::string themeStr;
std::tie(success, themeStr) = result;
if (!success) {
    logMessage("Error reading quiz theme from player socket: " +
               std::to_string(socket));
    return;
  }

  if (!themeStr.empty() && themeStr.back() == '\n') {
    themeStr.pop_back();
  }

  char theme = themeStr[0];
  logMessage("Player " + players[socket].nombre +
             " selected quiz theme: " + theme);

  auto &player = players[socket];
  bool &completed =
      (theme == '1') ? player.hasCompletedTech : player.hasCompletedGeneral;
  int &score = (theme == '1') ? player.techScore : player.generalScore;
  auto &questions = (theme == '1') ? techQuestions : generalQuestions;

  if (completed) {
    std::string msg = "Hai gia completato questo quiz!\n";
    if (!secureSend(socket, msg)) {
      logMessage("Error sending quiz completed message to player " +
                 player.nombre);
    }
    logMessage("Player " + player.nombre +
               " attempted to retake completed quiz.");
    return;
  }

  player.quizTheme = theme - '0';
  player.currentQuestion = 0;

  while (player.currentQuestion < questions.size()) {
    std::string questionMsg =
        std::string("\n\nQuiz - ") +
        ((player.quizTheme == 1) ? "Curiosita sulla tecnologia"
                                 : "Cultura Generale") +
        "\n++++++++++++++++++++++++++++++++++++++++\n" +
        questions[player.currentQuestion].question + "\n";

    if (!secureSend(socket, questionMsg)) {
      logMessage("Error sending question to player " + player.nombre);
    }

    logMessage("Sent question no." + std::to_string(player.currentQuestion) +
               " to player " + player.nombre);
    logMessage("Q: " + questions[player.currentQuestion].question);
    logMessage("A: " + questions[player.currentQuestion].answer);

    std::pair<bool, std::string> result = secureRead(socket, BUFFER_SIZE);
    bool success;
    std::string answer;
    std::tie(success, answer) = result;
    if (!success) {
      logMessage("Error reading answer from player " + player.nombre);
      break;
    }

    if (!answer.empty() && answer.back() == '\n') {
      answer.pop_back();
    }

    logMessage("Answer received from player " + player.nombre +
               " is: " + answer);

    if (answer == "show score") {
      logMessage("Player " + player.nombre + " requested scoreboard.");
      sendScoreboard(socket);
      continue;
    }

    if (answer == "endquiz") {
      std::string msg = "Quiz terminato.\n";
      if (!secureSend(socket, msg)) {
        logMessage("Error sending quiz ended message to player " +
                   player.nombre);
      }
      player.quizTheme = 0;
      logMessage("Player " + player.nombre + " ended the quiz.");
      return;
    }

    bool correct = (answer == questions[player.currentQuestion].answer);
    std::string resultMsg =
        correct ? "Risposta corretta!\n" : "Risposta sbagliata.\n";
    if (!secureSend(socket, resultMsg)) {
      logMessage("Error sending answer result to player " + player.nombre);
    }
    logMessage("Player: " + player.nombre + "responded " +
               (correct ? "correctly" : "incorrectly") + " to question no." +
               std::to_string(player.currentQuestion));

    if (correct)
      score++;
    player.currentQuestion++;
  }

  completed = true;
  player.quizTheme = 0;

  std::string finalMsg =
      "Quiz completato! Punteggio finale: " + std::to_string(score) + "/5\n";
  if (!secureSend(socket, finalMsg)) {
    logMessage("Error sending final score to player " + player.nombre);
  }
  logMessage("Player " + player.nombre +
             " completed the quiz with score: " + std::to_string(score));

result = secureRead(socket, BUFFER_SIZE);
bool rv;
std::string finishMsg;
std::tie(rv, finishMsg) = result;
if (!rv) {
    logMessage("Error reading finish message from player " + player.nombre);
    return;
  }

  if (!finishMsg.empty() && finishMsg.back() == '\n') {
    finishMsg.pop_back();
  }

  if (finishMsg != "TriviaCompleted") {
    if (player.hasCompletedTech && player.hasCompletedGeneral) {
      if (!secureSend(socket, "2")) {
        logMessage("Infromed both trivias completed" + player.nombre);
      }
    } else {
      if (!secureSend(socket, "1")) {
        logMessage("Informed one trivia completed" + player.nombre);
      }
    }
  }
}

void updateScoreboard() {
  while (true) {
    printScoreboard();
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

int main() {
  std::string techFile = "tech.txt";
  std::string generalFile = "general.txt";

  struct sigaction sa;
  sa.sa_handler = sigintHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, nullptr) == -1) {
    std::cerr << "Error setting up signal handler" << std::endl;
    return 1;
  }

  try {
    techQuestions = loadQuestions(techFile);
    generalQuestions = loadQuestions(generalFile);
    logMessage("Questions loaded successfully.");
  } catch (const std::exception &e) {
    std::cerr << "Error loading questions: " << e.what() << std::endl;
    logMessage("Error loading questions: " + std::string(e.what()));
    return 1;
  }

  std::thread scoreboardThread(updateScoreboard);
  scoreboardThread.detach();

  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("Socket creation failed");
    logMessage("Socket creation failed.");
    return 1;
  }

  fcntl(serverSocket, F_SETFL, O_NONBLOCK);

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(PORT);

  if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) <
      0) {
    perror("Bind failed");
    logMessage("Bind failed.");
    return 1;
  }

  if (listen(serverSocket, MAX_CLIENTS) < 0) {
    perror("Listen failed");
    logMessage("Listen failed.");
    return 1;
  }

  fd_set readfds;
  std::vector<int> clientSockets;

  while (running) {
    FD_ZERO(&readfds);
    FD_SET(serverSocket, &readfds);
    int maxSd = serverSocket;

    for (int sock : clientSockets) {
      FD_SET(sock, &readfds);
      maxSd = std::max(maxSd, sock);
    }

    if (select(maxSd + 1, &readfds, NULL, NULL, NULL) < 0) {
      perror("Select error");
      logMessage("Select error.");
      continue;
    }

    if (FD_ISSET(serverSocket, &readfds)) {
      struct sockaddr_in clientAddr;
      socklen_t addrLen = sizeof(clientAddr);
      int newSocket =
          accept(serverSocket, (struct sockaddr *)&clientAddr, &addrLen);

      if (newSocket >= 0) {
        // Set client socket to non-blocking
        fcntl(newSocket, F_SETFL, O_NONBLOCK);
        clientSockets.push_back(newSocket);
        logMessage("New connection accepted on socket " +
                   std::to_string(newSocket) + " from " +
                   inet_ntoa(clientAddr.sin_addr) + ":" +
                   std::to_string(ntohs(clientAddr.sin_port)));
      }
    }

    for (auto it = clientSockets.begin(); it != clientSockets.end();) {
      int sock = *it;
      if (FD_ISSET(sock, &readfds)) {
        auto playerIt = players.find(sock);
        if (playerIt == players.end()) {
          handleNewPlayer(sock);
        } else if (playerIt->second.quizTheme == 0) {
          handleQuiz(sock);
        }
      }

      /*Check if socket is still valid*/
      if (send(sock, "", 0, MSG_NOSIGNAL) < 0) {
        std::string playerInfo = players.find(sock) != players.end()
                                     ? " (player: " + players[sock].nombre + ")"
                                     : "";
        logMessage("Closing connection on socket " + std::to_string(sock) +
                   playerInfo);
        close(sock);
        players.erase(sock);
        it = clientSockets.erase(it);
        logMessage("Connection cleanup completed for socket: " +
                   std::to_string(sock));
      } else {
        ++it;
      }
    }
  }

  close(serverSocket);
  return 0;
}
