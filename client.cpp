#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <mutex>
#include <fstream>
#include <chrono>

#define BUFFER_SIZE 1024

std::ofstream logFile("cosole_log.txt", std::ios::app);
std::mutex playersMutex;

void logMessage(const std::string & message) {
    std::lock_guard<std::mutex> lock(playersMutex);
    logFile << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] " << message << std::endl;
}

void clearScreen() {
  system("clear");
}

void frontPage() {
  clearScreen();
  printf("\n==- Trivia Quiz Game -==\n");
  printf("+++++++++++++++++++++++++++++\n");
  printf("Menu\n");
  printf("1. Comincia una sessione di trivia\n");
  printf("2. Esci\n");
  printf("La tua scelta: ");
}

void sNickname() {
  clearScreen();
  printf("Trivia Quiz\n");
  printf("+++++++++++++++++++++++++++++\n");
  printf("Scegli un nickname (deve essere univoco): ");
}

void sTheme() {
  clearScreen();
  printf("Quiz disponibili\n");
  printf("+++++++++++++++++++++++++++++\n");
  printf("1 - Curiosita sulla tecnologia\n");
  printf("2 - Cultura generale\n");
  printf("+++++++++++++++++++++++++++++\n");
  printf("La tua scelta: ");
}

class TriviaClient {
  private:
    int sock;
    struct sockaddr_in servAddr;
    char buffer[BUFFER_SIZE];
    std::string nickname;
    bool isConnected;

    bool secureSend(const std::string& message) {
        size_t total_sent = 0;
        size_t len = message.length();
        
        while (total_sent < len) {
            ssize_t bytes_sent = send(sock, message.c_str() + total_sent, len - total_sent, 0);
            if (bytes_sent < 0) {
                logMessage("Error in send operation: " + std::string(strerror(errno)));
                return false;
            }
            total_sent += bytes_sent;
        }
        
        logMessage("Successfully sent " + std::to_string(total_sent) + " bytes: " + message);
        memset(buffer, 0, BUFFER_SIZE);
        return true;
    }

    bool secureReceive() {
        ssize_t total_read = 0;
        memset(buffer, 0, BUFFER_SIZE);
        
        while (total_read < BUFFER_SIZE - 1) {
            ssize_t bytes_read = read(sock, buffer + total_read, BUFFER_SIZE - 1 - total_read);
            if (bytes_read < 0) {
                logMessage("Error in receive operation: " + std::string(strerror(errno)));
                return false;
            }
            if (bytes_read == 0) {
                if (total_read == 0) {
                    logMessage("Connection closed by server");
                    return false;
                }
                break;
            }
            total_read += bytes_read;
            if (buffer[total_read - 1] == '\n' || strchr(buffer, '\0') != nullptr) {
                break;
            }
        }
        
        if (total_read > 0) {
            buffer[total_read] = '\0';
            logMessage("Received " + std::to_string(total_read) + " bytes from server");
            return true;
        }
        return false;
    }

    void handleServerDisconnect() {
        std::cout << "\nIl server si è disconnesso. Il quiz è terminato.\n";
        logMessage("Il server si è disconnesso. Il quiz è terminato.");
        close(sock);
        isConnected = false;
        sleep(2);
    }

    bool connectToServer(int port) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Errore nella creazione del socket\n";
            logMessage("Errore nella creazione del socket: " + std::string(strerror(errno)));
            return false;
        }
        logMessage("Socket created successfully");

        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(port);

        if (inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr) <= 0) {
            std::cerr << "Indirizzo non valido\n";
            logMessage("Indirizzo non valido");
            return false;
        }

        logMessage("Attempting to connect to server at 127.0.0.1:" + std::to_string(port));
        if (connect(sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
            std::cerr << "Connessione fallita\n";
            logMessage("Connessione fallita: " + std::string(strerror(errno)));
            return false;
        }

        logMessage("Successfully connected to server");
        isConnected = true;
        return true;
    }

    bool setNickname() {
        std::string input;
        while (true) {
            sNickname();
            std::getline(std::cin, input);
            logMessage("Nickname scelto: " + input);

            if (!isConnected) return false;

            if (!secureSend(input)) {
                handleServerDisconnect();
                return false;
            }

            if (!secureReceive()) {
                handleServerDisconnect();
                return false;
            }

            if (buffer[0] == '1') {
                nickname = input;
                return true;
            }
            std::cout << "Nickname già in uso. Riprova.\n";
            logMessage("Nickname già in uso. Riprova.");
            sleep(2);
        }
    }

    void playQuiz() {
        std::string input;
        sTheme();
        std::getline(std::cin, input);
        logMessage("Tema scelto: " + input);

        if (input != "1" && input != "2") return;

        if (!secureSend(input)) {
            logMessage("Errore nell'invio del tema");
            handleServerDisconnect();
            return;
        }

        while (isConnected) {
            if (!secureReceive()) {
                handleServerDisconnect();
                break;
            }
            std::string message(buffer);

            if (message.find("completato") != std::string::npos || message.find("già completato") != std::string::npos) {
                std::cout << message;
                logMessage("Quiz completato");
                sleep(3);
                break;
            }

            std::cout << message;
            logMessage("Received question: " + message);
            std::getline(std::cin, input);
            logMessage("Answer: " + input);

            if (input == "endquiz" || input == "show score") {
                if (!secureSend(input)) {
                    logMessage("Errore nell'invio del comando");
                    handleServerDisconnect();
                    break;
                }

                if (input == "endquiz") {
                    if (secureReceive()) {
                        std::cout << buffer;
                    }
                    break;
                }

                continue;
            }

            if (!secureSend(input)) {
                logMessage("Errore nell'invio della risposta");
                handleServerDisconnect();
                break;
            }

            if (!secureReceive()) {
                logMessage("Error in receiving question status");
                handleServerDisconnect();
                break;
            }
            std::string qStatus(buffer);
            printf("\033[2J\033[H");
            printf("\n******************************\n    %s\n******************************\n", qStatus.c_str());
            logMessage("Received question status: " + qStatus);
            sleep(2);
            printf("\033[2J\033[H");
        }
    }

  public:
    TriviaClient(): isConnected(false) {
        signal(SIGPIPE, SIG_IGN);
    }

    void start(int port) {
        while (true) {
            frontPage();
            std::string choice;
            std::getline(std::cin, choice);
            logMessage("Scelta: " + choice);

            if (choice == "2") break;
            if (choice != "1") continue;

            if (!isConnected && !connectToServer(port)) {
                std::cout << "Impossibile connettersi al server. Riprova più tardi.\n";
                sleep(2);
                continue;
            }

            if (setNickname()) {
                playQuiz();
            }
        }

        if (isConnected) {
            close(sock);
        }
    }
};

int main(int argc, char * argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " <porta>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    TriviaClient client;
    client.start(port);
    return 0;
}
