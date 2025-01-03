#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <csignal>

#define PORT 6969
#define BUFFER_SIZE 1024
#define MAX_CLIENT 10

std::ofstream logFile("server.log", std::ios::app);
std::shared_mutex playersMutex;
std::mutex questionsMutex;

struct Question {
  std::string question;
  std::string answer;
};

struct Player {
  std::string nickname;
  int currentTheme;
  int currentQuestionIndex;
  int techScore;
  int generalScore;
  bool hasCompletedTech;
  bool hasCompletedGeneral;
};

std::vector<Question> techQuestions;
std::vector<Question> generalQuestions;
std::vector<std::pair<int, Player>> players;

void logMessage(const std::string &message) {
  auto now = std::chrono::system_clock::now();
  logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " << message << std::endl;
}

void printScoreboard() {
  logMessage("********** PRINTING SCOREBOARD **********");
  std::stringstream ss;
  ss << "\033[2J\033[H";
  ss << "\t\033[1;36m    ==- Trivia Quiz -==\033[0m\n"
    << "++++++++++++++++++++++++++++++++++++++++\n"
    << "Temi disponibili:\n"
    << "1- Curiosita sulla tecnologia\n"
    << "2- Cultura Generale\n"
    << "+++++++++++++++++++++++++++++++++++++++\n";

  {
    std::shared_lock<std::shared_mutex> lock(playersMutex);
    ss << "Partecipanti attivi (" << players.size() << ")\n";
    for (const auto &player_pair : players) {
      const auto &player = player_pair.second;
      ss << "x " << player.nickname << "\n";
    }

    ss << "\nPuntaggi Tecnologia:\n";
    std::vector<std::pair<int, Player>> sortedPlayersTech = players;
    std::sort(sortedPlayersTech.begin(), sortedPlayersTech.end(), [](const auto &a, const auto &b) {
        return a.second.techScore > b.second.techScore;
        });
    for (const auto &player_pair : sortedPlayersTech) {
      const auto &player = player_pair.second;
      ss << "-> " << player.nickname << ": " << player.techScore << "/" << techQuestions.size() << "\n";
    }

    ss << "\nPuntaggi Cultura Generale:\n";
    std::vector<std::pair<int, Player>> sortedPlayersGeneral = players;
    std::sort(sortedPlayersGeneral.begin(), sortedPlayersGeneral.end(), [](const auto &a, const auto &b) {
        return a.second.generalScore > b.second.generalScore;
        });
    for (const auto &player_pair : sortedPlayersGeneral) {
      const auto &player = player_pair.second;
      ss << "-> " << player.nickname << ": " << player.generalScore << "/" << generalQuestions.size() << "\n";
    }

    ss << "\nQuiz Tecnologia completati:\n";
    for (const auto &player_pair : players) {
      const auto &player = player_pair.second;
      if (player.hasCompletedTech) {
        ss << "-> " << player.nickname << "\n";
      }
    }

    ss << "\nQuiz Cultura Generale completati:\n";
    for (const auto &player_pair : players) {
      const auto &player = player_pair.second;
      if (player.hasCompletedGeneral) {
        ss << "-> " << player.nickname << "\n";
      }
    }
  }

  ss << "----------------------------------------\n";
  printf("%s", ss.str().c_str());
  fflush(stdout);
}

std::vector<Question> loadQuestions(const std::string &filename) {
  try {
    std::lock_guard<std::mutex> lock(questionsMutex);
    std::vector<Question> questions;
    std::ifstream file(filename);
    if (!file.is_open()) {
      throw std::runtime_error("Unable to open file: " + filename);
    }
    std::string line;
    while (std::getline(file, line)) {
      size_t delimiterPos = line.find('|');
      if (delimiterPos != std::string::npos) {
        Question question;
        question.question = line.substr(0, delimiterPos);
        question.answer = line.substr(delimiterPos + 1);
        questions.push_back(question);
      }
    }
    return questions;
  } catch (const std::exception &e) {
    logMessage("Exception in loadQuestions: " + std::string(e.what()));
    return {};
  }
}

void removeClientData(int clientSocket) {
  {
    std::unique_lock<std::shared_mutex> lock(playersMutex);
    auto it = std::find_if(players.begin(), players.end(),
        [clientSocket](const std::pair<int, Player> &player) {
        return player.first == clientSocket;
        });
    if (it != players.end()) {
      logMessage("Removing data for client: " + it->second.nickname);
      players.erase(it);
    } else {
      logMessage("Client data not found for socket: " + std::to_string(clientSocket));
    }
  }
  printScoreboard();
}

bool secureSend(int clientSocket, const std::string &message) {
  try {
    uint32_t messageLength = htonl(message.size());
    if (send(clientSocket, &messageLength, sizeof(messageLength), 0) < 0) {
      logMessage("Error sending message length");
      return false;
    }
    if (send(clientSocket, message.c_str(), message.size(), 0) < 0) {
      logMessage("Error sending message");
      return false;
    }
    logMessage("Sent message of size: " + std::to_string(message.size()));
    return true;
  } catch (const std::exception &e) {
    logMessage("Exception in secureSend: " + std::string(e.what()));
    return false;
  }
}

bool secureReceive(int clientSocket, std::string &message) {
  try {
    uint32_t messageLength = 0;
    int bytesReceived = recv(clientSocket, &messageLength, sizeof(messageLength), 0);
    if (bytesReceived <= 0) {
      if (bytesReceived == 0) {
        struct sockaddr_in peerAddr;
        socklen_t peerAddrLen = sizeof(peerAddr);
        if (getpeername(clientSocket, (struct sockaddr *)&peerAddr, &peerAddrLen) == 0) {
          char clientIP[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &peerAddr.sin_addr, clientIP, sizeof(clientIP));
          logMessage("Client disconnected: " + std::string(clientIP) + ":" + std::to_string(ntohs(peerAddr.sin_port)));
          std::cout << "Client disconnected: " << clientIP << ":" << ntohs(peerAddr.sin_port) << std::endl;
        } else {
          logMessage("Client disconnected (error getting address)");
        }
        removeClientData(clientSocket);
      } else {
        logMessage("Error receiving message length");
      }
      return false;
    }
    messageLength = ntohl(messageLength);
    if (messageLength > BUFFER_SIZE) {
      logMessage("Message too large");
      return false;
    }
    std::vector<char> buffer(messageLength + 1);
    bytesReceived = recv(clientSocket, buffer.data(), messageLength, 0);
    if (bytesReceived <= 0) {
      if (bytesReceived == 0) {
        struct sockaddr_in peerAddr;
        socklen_t peerAddrLen = sizeof(peerAddr);
        if (getpeername(clientSocket, (struct sockaddr *)&peerAddr, &peerAddrLen) == 0) {
          char clientIP[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &peerAddr.sin_addr, clientIP, sizeof(clientIP));
          logMessage("Client disconnected: " + std::string(clientIP) + ":" + std::to_string(ntohs(peerAddr.sin_port)));
          std::cout << "Client disconnected: " << clientIP << ":" << ntohs(peerAddr.sin_port) << std::endl;
        } else {
          logMessage("Client disconnected (error getting address)");
        }
        removeClientData(clientSocket);
      } else {
        logMessage("Error receiving message");
      }
      return false;
    }
    buffer[messageLength] = '\0';
    message = buffer.data();
    logMessage("Received message of size: " + std::to_string(message.size()));
    logMessage("Received: " + message);
    return true;
  } catch (const std::exception &e) {
    logMessage("Exception in secureReceive: " + std::string(e.what()));
    return false;
  }
}

void sendScoreboard(int clientSocket) {
  try {
    std::ostringstream scoreboard;
    scoreboard << "\n=== PUNTEGGI ATTUALI ===\n\n";

    {
      std::shared_lock<std::shared_mutex> lock(playersMutex);

      std::vector<std::pair<std::string, int>> techScores;
      for (const auto &player : players) {
        techScores.emplace_back(player.second.nickname, player.second.techScore);
      }
      std::sort(techScores.begin(), techScores.end(),
          [](const auto &a, const auto &b) { return a.second > b.second; });

      scoreboard << "Quiz Tecnologia:\n";
      for (const auto &score : techScores) {
        scoreboard << score.first << ": " << score.second << " punti\n";
      }

      scoreboard << "\nQuiz Cultura Generale:\n";
      std::vector<std::pair<std::string, int>> generalScores;
      for (const auto &player : players) {
        generalScores.emplace_back(player.second.nickname, player.second.generalScore);
      }
      std::sort(generalScores.begin(), generalScores.end(),
          [](const auto &a, const auto &b) { return a.second > b.second; });

      for (const auto &score : generalScores) {
        scoreboard << score.first << ": " << score.second << " punti\n";
      }
    }

    secureSend(clientSocket, scoreboard.str());
  } catch (const std::exception &e) {
    logMessage("Exception in sendScoreboard: " + std::string(e.what()));
  }
}

void handleClient(int clientSocket) {
  logMessage("********** ENTERING handleClient **********");
  try {
    std::string message;
    if (!secureReceive(clientSocket, message)) {
      close(clientSocket);
      removeClientData(clientSocket);
      return;
    }
    if (message == "START") {
      while (true) {
        if (!secureReceive(clientSocket, message)) {
          close(clientSocket);
          removeClientData(clientSocket);
          return;
        }
        bool nicknameTaken = false;
        {
          std::shared_lock<std::shared_mutex> lock(playersMutex);
          for (const auto &player : players) {
            if (player.second.nickname == message) {
              nicknameTaken = true;
              break;
            }
          }
        }
        if (nicknameTaken) {
          secureSend(clientSocket, "NICKNAME_ALREADY_USED");
        } else {
          Player newPlayer = {message, 0, 0, 0, 0, false, false};
          {
            std::unique_lock<std::shared_mutex> lock(playersMutex);
            players.emplace_back(clientSocket, newPlayer);
          }
          secureSend(clientSocket, "OK");
          printScoreboard();
          break;
        }
      }

      while (true) {
        Player *currentPlayer = nullptr;
        {
          std::shared_lock<std::shared_mutex> lock(playersMutex);
          auto it = std::find_if(players.begin(), players.end(),
              [clientSocket](const std::pair<int, Player> &player) {
              return player.first == clientSocket;
              });
          if (it != players.end()) {
            currentPlayer = &it->second;
          } else {
            logMessage("Client data not found for socket: " + std::to_string(clientSocket));
            close(clientSocket);
            removeClientData(clientSocket);
            return;
          }
        }

        if (!secureReceive(clientSocket, message)) {
          close(clientSocket);
          removeClientData(clientSocket);
          return;
        }
        int theme;
        try {
          theme = std::stoi(message);
        } catch (const std::exception &e) {
          logMessage("Invalid input for theme selection: " + message);
          secureSend(clientSocket, "INVALID_THEME");
          continue;
        }
        if (theme != 1 && theme != 2) {
          secureSend(clientSocket, "INVALID_THEME");
          continue;
        }
        if ((theme == 1 && currentPlayer->hasCompletedTech) || (theme == 2 && currentPlayer->hasCompletedGeneral)) {
          secureSend(clientSocket, "ALREADY_COMPLETED");
          continue;
        }

        secureSend(clientSocket, "OK");
        auto &questions = (theme == 1) ? techQuestions : generalQuestions;
        for (size_t i = 0; i < questions.size(); ++i) {
          secureSend(clientSocket, questions[i].question);
          logMessage("Sent question: " + questions[i].question);
          if (!secureReceive(clientSocket, message)) {
            close(clientSocket);
            removeClientData(clientSocket);
            return;
          }
          if (message == "show score") {
            sendScoreboard(clientSocket);
            --i;
            logMessage("Sent scoreboard");
            continue;
          }
          if (message == "endquiz") {
            secureSend(clientSocket, "Quiz terminated.");
            logMessage("Quiz terminated.");
            close(clientSocket);
            removeClientData(clientSocket);
            return;
          }
          if (message == questions[i].answer) {
            {
              std::unique_lock<std::shared_mutex> lock(playersMutex);
              if (theme == 1) {
                currentPlayer->techScore++;
                logMessage("Player " + currentPlayer->nickname + " scored a point in tech quiz, now has: " + std::to_string(currentPlayer->techScore));
              } else {
                currentPlayer->generalScore++;
                logMessage("Player " + currentPlayer->nickname + " scored a point in general quiz, now has: " + std::to_string(currentPlayer->generalScore));
              }
            }
          }
          secureSend(clientSocket, (message == questions[i].answer) ? "CORRECT" : "INCORRECT");
          printScoreboard();
        }

        {
          std::unique_lock<std::shared_mutex> lock(playersMutex);
          if (theme == 1) {
            currentPlayer->hasCompletedTech = true;
          } else {
            currentPlayer->hasCompletedGeneral = true;
          }
        }

        if ((currentPlayer->hasCompletedTech && !currentPlayer->hasCompletedGeneral) ||
            (!currentPlayer->hasCompletedTech && currentPlayer->hasCompletedGeneral)) {
          secureSend(clientSocket, "COMPLETED_QUIZ");
          printScoreboard();
          continue;
        }
        {
          std::unique_lock<std::shared_mutex> lock(playersMutex);
          if (currentPlayer->hasCompletedTech && currentPlayer->hasCompletedGeneral) {
            secureSend(clientSocket, "BOTH_QUIZZES_COMPLETED");

            if (secureReceive(clientSocket, message) && message == "CLIENT_FINISHED") {
              break;
            }
          }
        }
      }
    }
  } catch (const std::exception &e) {
    logMessage("Exception in handleClient: " + std::string(e.what()));
  }
  close(clientSocket);
  removeClientData(clientSocket);
  logMessage("********** EXITING handleClient **********");
}

void signalHandler(int signum) {
  logMessage("Interrupt signal (" + std::to_string(signum) + ") received. Closing server...");
  {
    std::unique_lock<std::shared_mutex> lock(playersMutex);
    for (const auto &player : players) {
      secureSend(player.first, "SERVER_TERMINATED");
      logMessage("Sent SERVER_TERMINATED to player: " + player.second.nickname);
      close(player.first);
    }
  }
  logMessage("All client connections closed. Shutting down server.");
  exit(signum);
}

int main() {
  logMessage("------------------------------ SERVER START -----------------------------");
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  printScoreboard();
  try {
    techQuestions = loadQuestions("tech.txt");
    generalQuestions = loadQuestions("general.txt");

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
      perror("Socket creation failed");
      exit(EXIT_FAILURE);
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
      perror("Bind failed");
      exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, MAX_CLIENT) < 0) {
      perror("Listen failed");
      exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (true) {
      sockaddr_in clientAddr;
      socklen_t clientLen = sizeof(clientAddr);
      int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientLen);
      if (clientSocket < 0) {
        perror("Accept failed");
        continue;
      }
      std::thread(handleClient, clientSocket).detach();
    }

    close(serverSocket);
  } catch (const std::exception &e) {
    logMessage("Exception in main: " + std::string(e.what()));
    return EXIT_FAILURE;
  }
  return 0;
}

