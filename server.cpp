#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <netinet/in.h>
#include <signal.h>
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

/*Global variables*/
std::ofstream logFile("server.log", std::ios::app);
std::shared_mutex playersMutex;
std::mutex questionsMutex;

/*Questions structure*/
struct Question {
  std::string question;
  std::string answer;
};

/*Player structure(all data inside)*/
struct Player {
std::string nickname;
int currentTheme{0};
int currentQuestionIndex{0};
int techScore{0};
int generalScore{0};
bool hasCompletedTech{false};
bool hasCompletedGeneral{false};

Player(const std::string& name) : nickname(name) {}
Player() = default;
};

/*Vector to load questions*/
std::vector<Question> techQuestions;
std::vector<Question> generalQuestions;
/*Vector of players using pair to link player with socket*/
std::vector<std::pair<int, Player>> players;

/*Function to log messages with timestamps*/
void logMessage(const std::string &message) {
  auto now = std::chrono::system_clock::now();
  logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " << message << std::endl;
}

/*Function to print scoreboard, sorts by scores using stringstream to make easy format*/
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
    /*Sorting players by tech score*/
    std::vector<std::pair<int, Player>> sortedPlayersTech = players;
    std::sort(sortedPlayersTech.begin(), sortedPlayersTech.end(), [](const auto &a, const auto &b) {
        return a.second.techScore > b.second.techScore;
        });
    /*Printing sorted players*/
    for (const auto &player_pair : sortedPlayersTech) {
      const auto &player = player_pair.second;
      ss << "-> " << player.nickname << ": " << player.techScore << "/" << techQuestions.size() << "\n";
    }

    ss << "\nPuntaggi Cultura Generale:\n";
    /*Sortting players by general score*/
    std::vector<std::pair<int, Player>> sortedPlayersGeneral = players;
    std::sort(sortedPlayersGeneral.begin(), sortedPlayersGeneral.end(), [](const auto &a, const auto &b) {
        return a.second.generalScore > b.second.generalScore;
        });
    /*Printing sorted players*/
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

/*Function to load questions from file*/
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

/*After client acepting to finish the quiz, the server will send a message to the client to close the connection and remove the client data from the server*/
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

/*Function to send messages to the client, first sends the length of the message and then the message itself*/
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

/*Function to receive messages from the client, first receives the length of the message and then the message itself*/
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

/*Function to send the scoreboard to the client*/
void sendScoreboard(int clientSocket) {
  try {
    std::ostringstream scoreboard;
    scoreboard << "\n=== PUNTEGGI ATTUALI ===\n\n";

    {
      std::shared_lock<std::shared_mutex> lock(playersMutex);

    std::vector<std::pair<std::string, std::pair<int, int>>> scores;
    for (const auto &player : players) {
        scores.emplace_back(player.second.nickname, 
            std::make_pair(player.second.techScore, player.second.generalScore));
    }

    scoreboard << "Quiz Tecnologia:\n";
    std::sort(scores.begin(), scores.end(),
        [](const auto &a, const auto &b) { return a.second.first > b.second.first; });
    for (const auto &score : scores) {
        scoreboard << score.first << ": " << score.second.first << "/" 
                << techQuestions.size() << " punti\n";
    }

    scoreboard << "\nQuiz Cultura Generale:\n";
    std::sort(scores.begin(), scores.end(),
        [](const auto &a, const auto &b) { return a.second.second > b.second.second; });

    for (const auto &score : scores) {
    scoreboard << score.first << ": " << score.second.second << "/" 
                << generalQuestions.size() << " punti\n";
    }
    }

    secureSend(clientSocket, scoreboard.str());
  } catch (const std::exception &e) {
    logMessage("Exception in sendScoreboard: " + std::string(e.what()));
  }
}

/*Function to handle the client, first receives the nickname, then the theme selection, then the questions and answers, then the scoreboard and finally the end of the quiz*/
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
        Player newPlayer(message);
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
        bool isCompleted = false;
        {
        std::shared_lock<std::shared_mutex> lock(playersMutex);
        isCompleted = (theme == 1 && currentPlayer->hasCompletedTech) || 
                    (theme == 2 && currentPlayer->hasCompletedGeneral);
        }
        if (isCompleted) {
        logMessage("Player " + currentPlayer->nickname + " attempted to repeat completed theme: " + std::to_string(theme));
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
        std::string playerName;
        {
            std::unique_lock<std::shared_mutex> lock(playersMutex);
            auto it = std::find_if(players.begin(), players.end(),
                [clientSocket](const std::pair<int, Player>& player) {
                return player.first == clientSocket;
                });
            if (it != players.end()) {
            if (theme == 1) {
                it->second.techScore++;
                playerName = it->second.nickname;
                logMessage("Player " + playerName + " scored a point in tech quiz, now has: " + std::to_string(it->second.techScore));
            } else {
                it->second.generalScore++;
                playerName = it->second.nickname;
                logMessage("Player " + playerName + " scored a point in general quiz, now has: " + std::to_string(it->second.generalScore));
            }
            }
        }
          }
          secureSend(clientSocket, (message == questions[i].answer) ? "CORRECT" : "INCORRECT");
          printScoreboard();
        }

        {
        std::unique_lock<std::shared_mutex> lock(playersMutex);
        auto it = std::find_if(players.begin(), players.end(),
            [clientSocket](const std::pair<int, Player>& player) {
            return player.first == clientSocket;
            });
        
        if (it != players.end()) {
            if (theme == 1) {
                it->second.hasCompletedTech = true;
                logMessage("Player " + it->second.nickname + " completed tech quiz with score: " + 
                        std::to_string(it->second.techScore) + "/" + std::to_string(techQuestions.size()));
            } else {
                it->second.hasCompletedGeneral = true;
                logMessage("Player " + it->second.nickname + " completed general quiz with score: " + 
                        std::to_string(it->second.generalScore) + "/" + std::to_string(generalQuestions.size()));
            }
            currentPlayer = &it->second;
        }
        }

        bool oneQuizCompleted = false;
        {
        std::shared_lock<std::shared_mutex> lock(playersMutex);
        oneQuizCompleted = (currentPlayer->hasCompletedTech && !currentPlayer->hasCompletedGeneral) ||
                        (!currentPlayer->hasCompletedTech && currentPlayer->hasCompletedGeneral);
        }
        if (oneQuizCompleted) {
        logMessage("Player " + currentPlayer->nickname + " completed one quiz, can continue with the other");
        secureSend(clientSocket, "COMPLETED_QUIZ");
        printScoreboard();
        continue;
        }
        {
        bool bothCompleted = false;
        {
        std::shared_lock<std::shared_mutex> lock(playersMutex);
        bothCompleted = currentPlayer->hasCompletedTech && currentPlayer->hasCompletedGeneral;
        }

        if (bothCompleted) {
        secureSend(clientSocket, "BOTH_QUIZZES_COMPLETED");
        logMessage("Player " + currentPlayer->nickname + " completed both quizzes");
        printScoreboard();
        
        if (!secureReceive(clientSocket, message)) {
            logMessage("Failed to receive final confirmation from client");
        } else if (message != "CLIENT_FINISHED") {
            logMessage("Unexpected final message from client: " + message);
        }
        
        secureSend(clientSocket, "CLOSING_CONNECTION");
        break;
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

/*Function to handle the signal interrupt and terminate the server*/
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

/*Function to handle the SIGPIPE signal*/
void handleSigpipe(int sig) {
  logMessage("SIGPIPE received. Ignoring.");
}

/*Main function, loads questions, creates server socket, binds it, listens for clients and creates a thread for each client*/
int main() {
  logMessage("------------------------------ SERVER START -----------------------------");
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  signal(SIGPIPE, handleSigpipe);
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
