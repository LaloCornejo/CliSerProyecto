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
            logMessage("Errore nella creazione del socket");
            return false;
        }

        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(port);

        if (inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr) <= 0) {
            std::cerr << "Indirizzo non valido\n";
            logMessage("Indirizzo non valido");
            return false;
        }

        if (connect(sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
            std::cerr << "Connessione fallita\n";
            logMessage("Connessione fallita");
            return false;
        }

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

            ssize_t sent = send(sock, input.c_str(), input.length(), 0);
            logMessage("Sent: " + input);
            if (sent <= 0) {
                handleServerDisconnect();
                return false;
            }

            ssize_t bytes_read = read(sock, buffer, BUFFER_SIZE);
            if (bytes_read <= 0) {
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
        memset(buffer, 0, BUFFER_SIZE);
        std::string input;
        sTheme();
        std::getline(std::cin, input);
        logMessage("Tema scelto: " + input);

        if (input != "1" && input != "2") return;

        if (send(sock, input.c_str(), input.length(), 0) <= 0) {
            logMessage("Errore nell'invio del tema");
            handleServerDisconnect();
            return;
        }
        logMessage("Sent theme: " + input);

        while (isConnected) {
            ssize_t bytes_read = read(sock, buffer, BUFFER_SIZE);
            if (bytes_read <= 0) {
                handleServerDisconnect();
                break;
            }

            buffer[bytes_read] = '\0';
            std::string message(buffer);

            if (message.find("completato") != std::string::npos ||
                message.find("già completato") != std::string::npos) {
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
                if (send(sock, input.c_str(), input.length(), 0) <= 0) {
                    logMessage("Errore nell'invio del comando");
                    handleServerDisconnect();
                    break;
                }

                if (input == "endquiz") {
                    bytes_read = read(sock, buffer, BUFFER_SIZE);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        std::cout << buffer;
                    }
                    break;
                }

                continue;
            }

            if (send(sock, input.c_str(), input.length(), 0) <= 0) {
                logMessage("Errore nell'invio della risposta");
                handleServerDisconnect();
                break;
            }
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
