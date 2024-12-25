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
    bool hasCompletedTech;
    bool hasCompletedGeneral;
    int quizTheme; 
};

struct qQuestion {
    std::string question;
    std::string answer;
};

std::vector<qQuestion> techQuestions;
std::vector<qQuestion> generalQuestions;
std::map<int, Player> players;
std::mutex playersMutex;

std::vector<qQuestion> loadQuestions(std::string& filename) {
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

void moveCursor(int x, int y) {
    printf("\033[%d;%dH", x, y);
}

bool uniqueName(std::string name) {
    std::lock_guard<std::mutex> lock(playersMutex);
    for (const auto& player : players) {
        if (player.second.nombre == name) {
            return false;
        }
    }
    return true;
}

void printScoreboard() {
    moveCursor(1, 1);
    printf("\033[2J");
    
    // Header
    printf("\033[1;36m==- Trivia Quiz -==\033[0m\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Theme section
    moveCursor(4, 1);
    printf("Temi:\n");
    printf("1- Curiosita sulla tecnologia\n");
    printf("2- Cultura Generale\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Participants section
    moveCursor(9, 1);
    printf("\033[1mPartecipanti (%zu)\033[0m\n", players.size());
    int currentLine = 10;
    for (const auto& player : players) {
        moveCursor(currentLine++, 2);
        printf("• %s\n", player.second.nombre.c_str());
    }
    
    // Scores section
    currentLine += 1;
    moveCursor(currentLine++, 1);
    printf("\033[1mPunteggio tema 1\033[0m\n");
    for (const auto& player : players) {
        if(player.second.techScore != 0) {
            moveCursor(currentLine++, 2);
            printf("%s: %d\n", player.second.nombre.c_str(), player.second.techScore);
        }
    }
    
    currentLine += 1;
    moveCursor(currentLine++, 1);
    printf("\033[1mPunteggio tema 2\033[0m\n");
    for (const auto& player : players) {
        if(player.second.generalScore != 0) {
            moveCursor(currentLine++, 2);
            printf("%s: %d\n", player.second.nombre.c_str(), player.second.generalScore);
        }
    }
    
    // Completion status section
    currentLine += 1;
    moveCursor(currentLine++, 1);
    printf("\033[1mQuiz Tema 1 completato\033[0m\n");
    for (const auto& player : players) {
        if (player.second.hasCompletedTech) {
            moveCursor(currentLine++, 2);
            printf("%s\n", player.second.nombre.c_str());
        }
    }
    
    currentLine += 1;
    moveCursor(currentLine++, 1);
    printf("\033[1mQuiz Tema 2 completato\033[0m\n");
    for (const auto& player : players) {
        if (player.second.hasCompletedGeneral) {
            moveCursor(currentLine++, 2);
            printf("%s\n", player.second.nombre.c_str());
        }
    }
    
    currentLine += 1;
    moveCursor(currentLine, 1);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    moveCursor(currentLine + 2, 1);
    printf("\033[K"); 
    fflush(stdout);
}

void displayMessage(const std::string& message) {
    printf("\033[s");
    printf("\033[999;1H");
    printf("\033[K%s", message.c_str());
    printf("\033[u");
    fflush(stdout);
}

void updateScoreboard() {
    while (true) {
        printScoreboard();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
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

        displayMessage("Received nickname: " + std::string(buffer));

        buffer[bytes_read] = '\0';
        std::string nickname(buffer);
        
        if (uniqueName(nickname)) {
            validNickname = true;
            Player newPlayer;
            newPlayer.nombre = nickname;
            newPlayer.techScore = 0;
            newPlayer.generalScore = 0;
            newPlayer.hasCompletedTech = false;
            newPlayer.hasCompletedGeneral = false;
            newPlayer.quizTheme = 0;

            std::lock_guard<std::mutex> lock(playersMutex);
            players[socket] = newPlayer;
            
            send(socket, &validNickname, sizeof(validNickname), 0);
        } else {
            send(socket, &validNickname, sizeof(validNickname), 0);
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
        std::string questionMsg = "Question " + std::to_string(i + 1) + ": " + questions[i].question + "\n";
        send(socket, questionMsg.c_str(), questionMsg.length(), 0);

        int bytes = read(socket, buffer, BUFFER_SIZE);
        if (bytes <= 0) {
            break;
        }
        buffer[bytes] = '\0';
        std::string answer(buffer);
        answer.erase(std::remove(answer.begin(), answer.end(), '\n'), answer.end());

        bool correct = (answer == questions[i].answer);
        send(socket, &correct, sizeof(correct), 0);

        if (correct) {
            score++;
        }
    }

    completed = true;
    std::string finalMsg = "Quiz completed! Your final score: " + std::to_string(score) + "\n";
    send(socket, finalMsg.c_str(), finalMsg.length(), 0);
}

void run(int clientSocket) {
    auto it = players.find(clientSocket);
    if (it == players.end()) {
        handleNewPlayer(clientSocket);
        displayMessage("New player connected");
    } else {
        if (it->second.quizTheme == 0) {
            try {
                char buffer[BUFFER_SIZE];
                ssize_t bytesRead = read(clientSocket, buffer, BUFFER_SIZE);
                if (bytesRead > 0) {
                    std::string theme(buffer, bytesRead);
                    theme.erase(std::remove(theme.begin(), theme.end(), '\n'), theme.end());
                    int themeNum = std::stoi(theme);
                    send(clientSocket, "ok", 2, 0);
                    if (themeNum == 1 || themeNum == 2) {
                        handleQuiz(clientSocket, themeNum);
                    } else {
                        std::string errorMsg = "Invalid theme. Please choose theme 1 or 2\n";
                        send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
                    }
                }
            } catch (const std::invalid_argument& e) {
                std::string errorMsg = "Invalid input. Please enter a number (1 or 2)\n";
                send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
            } catch (const std::out_of_range& e) {
                std::string errorMsg = "Invalid theme. Please choose theme 1 or 2\n";
                send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
            }
        }
    }
}

void cleanupDisconnectedPlayers() {
    std::lock_guard<std::mutex> lock(playersMutex);
    for (auto it = players.begin(); it != players.end();) {
        if (send(it->first, "", 0, MSG_NOSIGNAL) < 0) {
            it = players.erase(it);
        } else {
            ++it;
        }
    }
}

int main(int argc, char* argv[]) {
    std::thread t(updateScoreboard);
    t.detach();

    std::string generalFile = "general.txt";
    std::string techFile = "tech.txt";

    try {
        generalQuestions = loadQuestions(generalFile);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    try {
        techQuestions = loadQuestions(techFile);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    int serverSocket, maxSd, sd, activity, newSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int playerSockets[MAX_CLIENTS] = {0};
    socklen_t addrLen;
    fd_set readfds;
    char buffer[BUFFER_SIZE];

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
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

    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;

    while (true) {
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

        activity = select(maxSd + 1, &readfds, NULL, NULL, &timeout);
        if ((activity < 0) && (errno != EINTR)) {
            perror("Error select");
        }

        if (FD_ISSET(serverSocket, &readfds)) {
            if ((newSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen)) < 0) {
                perror("Error accept");
                exit(EXIT_FAILURE);
            }

            displayMessage("New connection from " + std::string(inet_ntoa(clientAddr.sin_addr)));

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (playerSockets[i] == 0) {
                    displayMessage("Adding to list of sockets at index " + std::to_string(i) + " socket " + std::to_string(newSocket));
                    playerSockets[i] = newSocket;
                    break;
                }
            }
        }

        sleep(1);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = playerSockets[i];
            if (FD_ISSET(sd, &readfds)) {
                int valRead = read(sd, buffer, BUFFER_SIZE);
                if (valRead == 0) {
                    getpeername(sd, (struct sockaddr*)&clientAddr, &addrLen);
                     displayMessage("Host disconnected, ip " + std::string(inet_ntoa(clientAddr.sin_addr)) + ", port " + std::to_string(ntohs(clientAddr.sin_port)));
                     {
                        std::lock_guard<std::mutex> lock(playersMutex);
                        players.erase(sd);
                    }
                    close(sd);
                    playerSockets[i] = 0;
                } else {
                  displayMessage("Running quiz for socket " + std::to_string(sd));
                    run(sd);
                }
            }
        }

        cleanupDisconnectedPlayers(); 
    }

    close(serverSocket);
    return 0;
}
