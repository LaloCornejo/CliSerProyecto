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

#define PORT 1234
#define MAX_SIZE 1024
#define MAX_CLIENT 1024

std::ofstream logFile("server.log", std::ios::app);
std::mutex playersMutex;
std::mutex questionsMutex;

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

// Forward declarations
void playTrivia(int socket);
void handleNewPlayer(int socket);
void printScoreboard();
void sendScoreboard(int socket);
std::vector<Question> handleThemeSelection(int socket);

std::pair<int, Player>& find_player_by_socket(int socket) {
    auto it = std::find_if(players.begin(), players.end(),
        [socket](const std::pair<int, Player>& p) { return p.first == socket; });
    return *it;
}

void logMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(playersMutex);
    auto now = std::chrono::system_clock::now();
    logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " 
            << message << std::endl;
}

bool secureSend(int socket, const std::string &message) {
    size_t totalSent = 0;
    size_t messageLength = message.length();

    while (totalSent < messageLength) {
        ssize_t sent = send(socket, message.c_str() + totalSent,
                          messageLength - totalSent, 0);
        if (sent == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue;
            }
            logMessage("Error sending message to client");
            return false;
        }
        totalSent += sent;
    }
    logMessage("Successfully sent total of " + std::to_string(totalSent) + 
               " bytes to client");
    return true;  
}

bool secureReceive(int socket, std::string &message) {
    char buffer[MAX_SIZE] = {0};  
    ssize_t bytesRead = recv(socket, buffer, MAX_SIZE - 1, 0);  

    if (bytesRead <= 0) {  
        logMessage("Client disconnected or error receiving message");
        return false;
    }

    message = std::string(buffer, bytesRead);
    logMessage("Received " + std::to_string(bytesRead) + " bytes from client: " + message);
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
    auto& player_pair = find_player_by_socket(socket);
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
        if (!secureSend(socket, "1")) {
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
        if (!secureSend(socket, "0")) {
          logMessage("Error sending message to client");
          perror("Error sending message to client");
          return;
        }
      }
      player.currentQ++;
    }
}

void run(int socket) {
    try {
        techQuestions = loadQuestions("tech.txt");
        generalQuestions = loadQuestions("general.txt");
        logMessage("Loaded questions");
        
        handleNewPlayer(socket);
        
        
        auto it = std::find_if(players.begin(), players.end(),
                            [socket](const std::pair<int, Player> &p) { return p.first == socket; });
        if (it == players.end()) {
            logMessage("Player not found for socket: " + std::to_string(socket));
            return;
        }
        
        Player &player = it->second;
        bool bothThemesCompleted = player.hasCompletedTech && 
                                 player.hasCompletedGeneral;

        while (!bothThemesCompleted) {
            handleThemeSelection(socket);
            bothThemesCompleted = player.hasCompletedTech && 
                                player.hasCompletedGeneral;
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
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.techScore > 0) {
            ss << player.nombre << ": " << player.techScore << "/" 
            << techQuestions.size() << "\n";
        }
    }
    
    ss << "\nGeneral Knowledge Scores:\n";
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.generalScore > 0) {
            ss << player.nombre << ": " << player.generalScore << "/" 
            << generalQuestions.size() << "\n";
        }
    }
    
    secureSend(socket, ss.str());
}

void printScoreboard() {
    std::stringstream ss;
    ss << "\033[2J\033[H";  // Clear screen and move cursor to top
   
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
    for (const auto& player_pair : players) {
        const auto& player = player_pair.second;
        if (player.techScore >= 0) {
            ss << "-> " << player.nombre << ": " << player.techScore << "/" 
            << techQuestions.size() << "\n";
        }
    }
    
    ss << "\nPuntaggi Cultura Generale:\n";
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

void handleNewPlayer(int socket) {
    std::string nickname;
    if (!secureReceive(socket, nickname)) {
        logMessage("Error receiving nickname");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(playersMutex);
        // Check if nickname is already taken
        for (const auto& player_pair : players) {
            if (player_pair.second.nombre == nickname) {
                secureSend(socket, "Nickname already taken");
                return;
            }
        }

        // Create new player
        Player newPlayer;
        newPlayer.nombre = nickname;
        newPlayer.currentT = 0;
        newPlayer.currentQ = 0;
        newPlayer.techScore = 0;
        newPlayer.generalScore = 0;
        newPlayer.hasCompletedTech = false;
        newPlayer.hasCompletedGeneral = false;

        players.push_back(std::make_pair(socket, newPlayer));
    }

    secureSend(socket, "Welcome " + nickname);
    logMessage("New player joined: " + nickname);
    printScoreboard();
}

std::vector<Question> handleThemeSelection(int socket) {
    std::string theme;
    if (!secureReceive(socket, theme)) {
        logMessage("Error receiving theme selection");
        return std::vector<Question>();
    }

    auto& player_pair = find_player_by_socket(socket);
    auto& player = player_pair.second;

    if (theme == "1") {  // Technology theme
        if (player.hasCompletedTech) {
            secureSend(socket, "You have already completed the technology quiz");
            return std::vector<Question>();
        }
        return techQuestions;
    } else if (theme == "2") {  // General knowledge theme
        if (player.hasCompletedGeneral) {
            secureSend(socket, "You have already completed the general knowledge quiz");
            return std::vector<Question>();
        }
        return generalQuestions;
    }

    secureSend(socket, "Invalid theme selection");
    return std::vector<Question>();
}

int main() {
    logMessage("===== Server started =====");
    int serverfd;
    struct sockaddr_in serverAddr;
    int addrLen = sizeof(serverAddr);

    // Socket creation (removed duplicate socket creation)
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        logMessage("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        logMessage("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Socket bind failed");
        logMessage("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(serverfd, MAX_CLIENT) < 0) {
        perror("Socket listen failed");
        logMessage("Socket listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);
    printScoreboard();

    while (true) {
        int newSocket;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        if ((newSocket = accept(serverfd, (struct sockaddr *)&clientAddr, &clientLen)) < 0) {
            perror("Socket accept failed");
            logMessage("Socket accept failed");
            continue;  
        }

        printf("New connection, socket fd is %d, ip is: %s, port: %d\n", 
               newSocket, inet_ntoa(clientAddr.sin_addr), 
               ntohs(clientAddr.sin_port));
        logMessage("New connection, socket fd is " + std::to_string(newSocket) + 
                  ", ip is: " + inet_ntoa(clientAddr.sin_addr) + 
                  ", port: " + std::to_string(ntohs(clientAddr.sin_port)));

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            logMessage("Fork failed");
            close(newSocket);
        } else if (pid == 0) {
            close(serverfd);  
            run(newSocket);
            exit(0);  
        } else {
            close(newSocket);  
        }
    }
    return 0;
}
