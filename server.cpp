#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <vector>
#include <sstream>
#include <chrono>
#include <errno.h>
#include <iostream>
#include <algorithm>
#include <sys/socket.h>

#define PORT 1234
#define BUFFER_SIZE 1024
#define MAX_CLIENT 10

std::ofstream logFile("server.log", std::ios::app);
std::mutex playersMutex;
std::mutex questionsMutex;

std::string logSeparator = "----------------------------------\n\tSERVER LOG\n----------------------------------\n";

struct Question {
    std::string question;
    std::string answer;
    bool operator==(const Question& other) const {
        return question == other.question && answer == other.answer;
    }
};

struct Player {
    std::string nombre;
    int currentT;
    int currentQ;
    int techScore;
    int generalScore;
    bool hasCompletedTech;
    bool hasCompletedGeneral;
};

std::vector<Question> techQuestions;
std::vector<Question> generalQuestions;
std::vector<std::pair<int, Player>> players;

void playTrivia(int socket);
bool handleNewPlayer(int socket);
void printScoreboard();
void sendScoreboard(int socket);
std::vector<Question> handleThemeSelection(int socket);

void logMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(playersMutex);
    auto now = std::chrono::system_clock::now();
    logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " 
            << message << std::endl;
}

std::pair<int, Player>* find_player_by_socket(int socket) {
    logMessage("-------- Find Player By Socket --------");
    auto it = std::find_if(players.begin(), players.end(),
        [socket](const std::pair<int, Player>& p) { return p.first == socket; });
    
    if (it != players.end()) {
        return &(*it);
    } else {
        return nullptr;
    }
}

bool secureSend(int clientSocket, const std::string &message) {
      int messageLength = strlen(message.c_str());
      int totalSent = htonl(messageLength);
      send(clientSocket, &totalSent, sizeof(totalSent), 0);
      if (send(clientSocket, message.c_str(), messageLength, 0) < 0) {
        printf("Errore nell'invio del messaggio\n");
        logMessage("Error sending message");
        return false;
      }
      logMessage("Sent total " + std::to_string(messageLength) + " bytes");
      logMessage("Sent message: " + message);
      return true;
    }

bool secureReceive(int socket, std::string &message) {
    char buffer[BUFFER_SIZE] = {0};
    int received = 0;

        int bytes_read = recv(socket, &received, sizeof(received), 0);
        if (bytes_read < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
              return false;
            }
            printf("Errore nella ricezione del messaggio\n");
            logMessage("Error receiving message");
            return false;
        } else if (bytes_read == 0) {
            printf("Connessione chiusa dal server\n");
            logMessage("Connection closed by server");
            return false;
        }
        received = ntohl(received);
        logMessage("Received total " + std::to_string(received) + " bytes");
        bytes_read = recv(socket, buffer, received, 0);
        if (bytes_read < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                return false;
            }
            printf("Errore nella ricezione del messaggio\n");
            logMessage("Error receiving message");
            return false;
        } else if (bytes_read == 0) {
            printf("Connessione chiusa dal server\n");
            logMessage("Connection closed by server");
            return false;
        }
        buffer[bytes_read] = '\0';
        logMessage("Received message: " + std::string(buffer));
        message = buffer;
    return true;
}

std::vector<Question> loadQuestions(const std::string &filename) {
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
}

void playTrivia(int socket) {
    logMessage("-------- Play Trivia --------");
    auto player_ptr = find_player_by_socket(socket);
    if (!player_ptr) {
        logMessage("Player not found for socket: " + std::to_string(socket));
        return;
    }
    logMessage("Player found for socket: " + std::to_string(socket));
    auto& player_pair = *player_ptr;
    auto& player = player_pair.second;
    std::vector<Question> questions = handleThemeSelection(socket);
    if (questions.empty()) {
        return;
    }
    while (player.currentQ < questions.size()) {
        if (!secureSend(socket, questions[player.currentQ].question)) {
            logMessage("Error sending message to client");
            perror("Error sending message to client");
            return;
        }
        logMessage("Sent question no. " + std::to_string(player.currentQ) + 
                   " to player " + player.nombre);
        logMessage("Q: " + questions[player.currentQ].question);
        logMessage("A: " + questions[player.currentQ].answer);
        std::string answer;
        if (!secureReceive(socket, answer)) {
            logMessage("Error receiving answer from client");
            perror("Error receiving answer from client");
            return;
        }
        logMessage("Received answer from player " + player.nombre + ": " + answer);
        if (answer.empty() && answer.back() == '\n') {
            answer.pop_back();
        }
        if (answer == "show score" || answer == "endquiz") {
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
                break;
            }
        }
        if (answer == questions[player.currentQ].answer) {
            if (!secureSend(socket, "CORRECT")) {
                logMessage("Error sending message to client");
                perror("Error sending message to client");
                return;
            }
            if (questions == techQuestions) {
                player.techScore++;
            } else {
                player.generalScore++;
            }
        } else {
            if (!secureSend(socket, "INCORRECT")) {
                logMessage("Error sending message to client");
                perror("Error sending message to client");
                return;
            }
        }
        player.currentQ++;
    }
    if (player.currentT == 1) {
        player.hasCompletedTech = true;
        player.currentQ = 0;
        logMessage("Player " + player.nombre + " has completed the technology quiz");
    } else if (player.currentT == 2) {
        player.hasCompletedGeneral = true;
        player.currentQ = 0;
        logMessage("Player " + player.nombre + " has completed the general knowledge quiz");
    }
    printScoreboard();
    std::string finalMsg = "COMPLETED_QUIZ";
    if (!secureSend(socket, finalMsg)) {
        logMessage("Error sending quiz completed message to player " + player.nombre);
    }
}

void run(int socket) {
  logMessage("-------- Run --------");
  logMessage("Socket: " + std::to_string(socket));
    try {
        techQuestions = loadQuestions("tech.txt");
        generalQuestions = loadQuestions("general.txt");
        logMessage("Loaded questions");
        
        while (!handleNewPlayer(socket)) {
            continue;
        }
        auto player_ptr = find_player_by_socket(socket);
        if (!player_ptr) {
            logMessage("Player not found for socket: " + std::to_string(socket));
            return;
        }
        Player &player = player_ptr->second;
        bool bothThemesCompleted = player.hasCompletedTech && 
                                   player.hasCompletedGeneral;
        while (!bothThemesCompleted) {
            bothThemesCompleted = player.hasCompletedTech && 
                                  player.hasCompletedGeneral;
            playTrivia(socket);
            if (player.currentT == 1 && player.currentQ == techQuestions.size()) {
                player.hasCompletedTech = true;
                logMessage("Player " + player.nombre + " has completed the technology quiz");
            }
            if (player.currentT == 2 && player.currentQ == generalQuestions.size()) {
                player.hasCompletedGeneral = true;
                logMessage("Player " + player.nombre + " has completed the general knowledge quiz");
            }
        }
    } catch (const std::exception &e) {
        logMessage("Error in run: " + std::string(e.what()));
    }
}

void sendScoreboard(int socket) {
    std::stringstream ss;
    ss << "\nScoreboard:\n";
    ss << "Technology Quiz Scores:\n";
    
    std::lock_guard<std::mutex> lock(playersMutex);

    std::sort(players.begin(), players.end(), [](const std::pair<int, Player>& a, const std::pair<int, Player>& b) {
        return a.second.techScore > b.second.techScore;
    });

    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
            ss << player.nombre << ": " << player.techScore << "/" 
            << techQuestions.size() << "\n";
    }
    
    ss << "\nGeneral Knowledge Scores:\n";

    std::sort(players.begin(), players.end(), [](const std::pair<int, Player>& a, const std::pair<int, Player>& b) {
        return a.second.generalScore > b.second.generalScore;
    });

    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
            ss << player.nombre << ": " << player.generalScore << "/" 
            << generalQuestions.size() << "\n";
    }
    
    secureSend(socket, ss.str());
}

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

    ss << "Partecipanti attivi (" << players.size() << ")\n";
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        ss << "x " << player.nombre << "\n";
    }
    
    ss << "\nPuntaggi Tecnologia:\n";

    // Sort players by tech score in decreasing order
    std::sort(players.begin(), players.end(), [](const std::pair<int, Player>& a, const std::pair<int, Player>& b) {
        return a.second.techScore > b.second.techScore;
    });

    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.techScore >= 0) {
            ss << "-> " << player.nombre << ": " << player.techScore << "/" 
            << techQuestions.size() << "\n";
        }
    }
    
    ss << "\nPuntaggi Cultura Generale:\n";

    // Sort players by general score in decreasing order
    std::sort(players.begin(), players.end(), [](const std::pair<int, Player>& a, const std::pair<int, Player>& b) {
        return a.second.generalScore > b.second.generalScore;
    });

    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.generalScore >= 0) {
            ss << "-> " << player.nombre << ": " << player.generalScore << "/" 
            << generalQuestions.size() << "\n";
        }
    }

    ss << "\nQuiz Tecnologia completati:\n";
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.hasCompletedTech) {
            ss << "-> " << player.nombre << "\n";
        }
    }

    ss << "\nQuiz Cultura Generale completati:\n";
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.hasCompletedGeneral) {
            ss << "-> " << player.nombre << "\n";
        }
    }

    ss << "----------------------------------------\n";
    
    printf("%s\n", ss.str().c_str());
    fflush(stdout);
}

bool handleNewPlayer(int socket) {
  logMessage("-------- New Player --------");
  logMessage("Socket: " + std::to_string(socket));
    std::string nickname;
    logMessage("Waiting for nickname from player " + std::to_string(socket));
    if (!secureReceive(socket, nickname)) {
        logMessage("Error receiving nickname");
        exit(EXIT_FAILURE);
    }
    //errase last line of the logFile 
    logFile.seekp(-1, std::ios_base::end);

    /*{*/
    /*    std::lock_guard<std::mutex> lock(playersMutex);*/
        
        for (const auto& player_pair : players) {
            if (player_pair.second.nombre == nickname) {
                secureSend(socket, "NICKNAME_ALREADY_USED");
                logMessage("Nickname already used: " + nickname);
                return false;
            }
        }

        Player newPlayer = {
            nickname,
            0, 0, 0, 0,
            false, false
        };
        printf("New player joined: %s with socket no: \n", nickname.c_str(), socket);

        players.push_back(std::make_pair(socket, newPlayer));
        logMessage("New player joined: " + nickname);
      /*for (const auto& player : players) {*/
      /*  logMessage("Player: " + player.second.nombre);*/
      /*}*/
    /*}*/

    secureSend(socket, "OK");
    logMessage("New player joined: " + nickname);
    printScoreboard();
    return true;
}

std::vector<Question> handleThemeSelection(int socket) {
    logMessage("-------- Theme Selection --------");
    std::string theme;
    if (!secureReceive(socket, theme)) {
        logMessage("Error receiving theme selection");
        return std::vector<Question>();
    }

    auto player_opt = find_player_by_socket(socket);
    if (!player_opt) {
        logMessage("Player not found for socket: " + std::to_string(socket));
        return std::vector<Question>();
    }

    auto& player_pair = *player_opt;
    auto& player = player_pair.second;

    logMessage("Received theme selection from player " + player.nombre + ": " + theme);

    if (theme == "1") {  
        if (player.hasCompletedTech) {
            secureSend(socket, "You have already completed the technology quiz");
        }
        logMessage("Quiz not completed yet");
        if (!secureSend(socket, "OK")) {
          return std::vector<Question>();
        }
        logMessage("Loaded technology questions for player " + player.nombre);
        player.currentT = 1;
        return techQuestions;
    } else if (theme == "2") {  
        if (player.hasCompletedGeneral) {
            secureSend(socket, "You have already completed the general knowledge quiz");
        }
        if (!secureSend(socket, "OK")) {
          return std::vector<Question>();
        }
        logMessage("Loaded general knowledge questions for player " + player.nombre);
        player.currentT = 2;
        return generalQuestions;
    }
    secureSend(socket, "INVALID_THEME");
    logMessage("Invalid theme selection from player " + player.nombre);
    return std::vector<Question>();
}

int main() {
    logMessage(logSeparator);
    int serverfd, newSocket, maxSd, sd, activity;
    struct sockaddr_in address;
    int addrLen = sizeof(address), clientScoket[MAX_CLIENT] = {0};
    fd_set readfds;
    char buffer[BUFFER_SIZE] = {0};

    // Socket creation (removed duplicate socket creation)
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        logMessage("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(serverfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Socket bind failed");
        logMessage("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(serverfd, MAX_CLIENT) < 0) {
        perror("Socket listen failed");
        logMessage("Socket listen failed");
        exit(EXIT_FAILURE);
    }

    printScoreboard();
    printf("Server TCP listening on port %d\n", PORT);

    while (true) {
      FD_ZERO(&readfds);
        
      FD_SET(serverfd, &readfds);
      maxSd = serverfd;

      for (int i = 0; i < MAX_CLIENT; i++) {
        sd = clientScoket[i];
        if (sd > 0) {
          FD_SET(sd, &readfds);
        }
        if (sd > maxSd) {
          maxSd = sd;
        }
      }

      activity = select(maxSd + 1, &readfds, NULL, NULL, NULL);
      if ((activity < 0) && (errno != EINTR))  {
        printf("Select error\n");
        logMessage("Select error");
      }

      if (FD_ISSET(serverfd, &readfds)) {
        if ((newSocket = accept(serverfd, (struct sockaddr *)&address, (socklen_t*)&addrLen)) < 0) {
          perror("Socket accept failed");
          logMessage("Socket accept failed");
          continue;  
        }

        printf("New connection, socket is %d, ip is: %s, port: %d\n", 
               newSocket, inet_ntoa(address.sin_addr), 
               ntohs(address.sin_port));
        logMessage("New connection, socket is " + std::to_string(newSocket) + 
                  ", ip is: " + inet_ntoa(address.sin_addr) + 
                  ", port: " + std::to_string(ntohs(address.sin_port)));

        for (int i = 0; i < MAX_CLIENT; i++) {
          if (clientScoket[i] == 0) {
            clientScoket[i] = newSocket;
            printf("Adding to list of sockets as %d\n", i);
            logMessage("Adding to list of sockets as " + std::to_string(i));
            break;
          }
        }
      }

      for (int i = 0; i < MAX_CLIENT; i++) {
        sd = clientScoket[i];
        if (FD_ISSET(sd, &readfds)) {
          if (read(sd, buffer, BUFFER_SIZE) == 0) {
            getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrLen);
            printf("Host disconnected, ip %s, port %d\n", 
                   inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            logMessage("Host disconnected, ip " + std::string(inet_ntoa(address.sin_addr)) + 
                      ", port " + std::to_string(ntohs(address.sin_port)));
            close(sd);
            clientScoket[i] = 0;
          } else {
            run(sd);
          }
        }
      }
  }
  return 0;
}
