#include <arpa/inet.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#define BUFFER_SIZE 1024

std::ofstream logFile("client.log", std::ios::out | std::ios::app);
std::mutex logMutex;

void logMessage(const std::string &message) {
  std::lock_guard<std::mutex> lock(logMutex);
  auto now = std::chrono::system_clock::now();
  logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " << message
    << std::endl;
}

void clearScreen() { std::cout << "\033[2J\033[1;1H"; }

void showMenu() {
  clearScreen();
  std::cout << "\n==- Trivia Quiz Game -==\n"
    << "+++++++++++++++++++++++++++++\n"
    << "Menu\n"
    << "1. Comincia una sessione di trivia\n"
    << "2. Esci\n"
    << "La tua scelta: ";
}

class TriviaClient {
  private:
    int clientSocket;
    std::string nickname;
    int theme;
    int port;
    bool isConnected;

    bool secureSend(const std::string &message) {
      uint32_t messageLength = htonl(message.size());
      if (send(clientSocket, &messageLength, sizeof(messageLength), 0) < 0) {
        logMessage("Error sending message length: " + std::to_string(errno));
        return false;
      }

      if (send(clientSocket, message.c_str(), message.size(), 0) < 0) {
        logMessage("Error sending message: " + std::to_string(errno));
        return false;
      }

      logMessage("Sent: " + message);
      return true;
    }

    bool secureReceive(std::string &message) {
      uint32_t messageLength = 0;
      int bytesReceived = recv(clientSocket, &messageLength, sizeof(messageLength), 0);
      if (bytesReceived <= 0) {
        logMessage("Error receiving message length");
        return false;
      }

      messageLength = ntohl(messageLength);
      if (messageLength > BUFFER_SIZE) {
        logMessage("Message too large: " + std::to_string(messageLength));
        return false;
      }

      std::vector<char> buffer(messageLength + 1);
      bytesReceived = recv(clientSocket, buffer.data(), messageLength, 0);
      if (bytesReceived <= 0) {
        logMessage("Error receiving message");
        return false;
      }

      buffer[bytesReceived] = '\0';
      message = buffer.data();
      logMessage("Received: " + message);

      // Check for server termination message
      if (message == "SERVER_TERMINATED") {
        handleServerTermination();
        return false; // Indicate that the connection should be closed
      }

      return true;
    }

    void handleServerTermination() {
      clearScreen();
      std::cout << "Il server è stato terminato. Il gioco non è più disponibile.\n";
      std::cin.get();
    }

    void setNickname() {
      while (true) {
        clearScreen();
        std::cout << "Trivia Quiz\n"
          << "+++++++++++++++++++++++++++++\n"
          << "Scegli un nickname (deve essere univoco): ";

        std::getline(std::cin, nickname);

        if (nickname.empty()) {
          std::cout
            << "Nickname non può essere vuoto. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }

        if (!secureSend(nickname)) {
          std::cout
            << "Errore nell'invio del nickname. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }

        std::string response;
        if (!secureReceive(response)) {
          std::cout << "Errore nella ricezione della risposta. Premi invio per "
            "riprovare...";
          std::cin.get();
          continue;
        }

        if (response == "OK") {
          return;
        }

        if (response == "NICKNAME_ALREADY_USED") {
          std::cout << "Nickname già in uso. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }
      }
    }

    bool selectTheme() {
      while (true) {
        clearScreen();
        std::cout << "Quiz disponibili\n"
          << "+++++++++++++++++++++++++++++\n"
          << "1 - Curiosita sulla tecnologia\n"
          << "2 - Cultura generale\n"
          << "+++++++++++++++++++++++++++++\n"
          << "La tua scelta: ";

        std::string input;
        std::getline(std::cin, input);

        if (input != "1" && input != "2") {
          std::cout << "Scelta non valida. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }

        if (!secureSend(input)) {
          std::cout
            << "Errore nell'invio della scelta. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }

        std::string response;
        if (!secureReceive(response)) {
          std::cout << "Errore nella ricezione della risposta. Premi invio per "
            "riprovare...";
          std::cin.get();
          continue;
        }

        if (response == "OK") {
          theme = std::stoi(input);
          return true;
        }

        if (response == "INVALID_THEME") {
          std::cout << "Tema non valido. Premi invio per riprovare...";
          std::cin.get();
          continue;
        }

        if (response == "ALREADY_COMPLETED") {
          std::cout << "Hai già completato questo tema. Scegli un altro tema.\nPremi invio per continuare...";
          std::cin.get();
          continue;
        }

        std::cout << response << "\nPremi invio per continuare...";
        std::cin.get();
        return false;
      }
    }

    void playQuiz() {
      while (true) {
        std::string question;
        if (!secureReceive(question)) {
          std::cout << "Connessione con il server persa.\n";
          return;
        }

        if (question == "COMPLETED_QUIZ") {
          std::cout << "\n*** Quiz Completato ***\n";
          if (!selectTheme()) {
            return;
          }
          continue;
        }

        clearScreen();
        std::string themeStr =
          (theme == 1) ? "Curiosita sulla tecnologia" : "Cultura generale";
        std::cout << "\nQuiz - " << themeStr << "\n"
          << "********************************\n"
          << question << "\n"
          << "********************************\n"
          << "Risposta (o 'show score'/'endquiz'): ";

        std::string answer;
        std::getline(std::cin, answer);

        if (!secureSend(answer)) {
          std::cout << "Errore nell'invio della risposta.\n";
          return;
        }

        if (answer == "show score") {
          std::string scoreboard;
          if (!secureReceive(scoreboard)) {
            std::cout << "Errore nella ricezione del punteggio.\n";
            return;
          }
          clearScreen();
          std::cout << scoreboard << "\nPremi invio per continuare...";
          std::cin.get();
          continue;
        }

        if (answer == "endquiz") {
          std::string endMessage;
          if (!secureReceive(endMessage)) {
            std::cout << "Errore nella ricezione del messaggio finale.\n";
            return;
          }
          std::cout << endMessage << "\nPremi invio per continuare...";
          std::cin.get();
          break;
        }

        std::string result;
        if (!secureReceive(result)) {
          std::cout << "Errore nella ricezione del risultato.\n";
          return;
        }

        clearScreen();
        std::cout << "\n********************************\n"
          << "\t" << result << "\n"
          << "********************************\n"
          << "Premi invio per continuare...";
        std::cin.get();
      }
    }

  public:
    TriviaClient(int serverPort) : port(serverPort), isConnected(false) {}

    bool connectToServer() {
      struct sockaddr_in serverAddr;

      clientSocket = socket(AF_INET, SOCK_STREAM, 0);
      if (clientSocket < 0) {
        logMessage("Creazione socket fallita");
        return false;
      }

      serverAddr.sin_family = AF_INET;
      serverAddr.sin_port = htons(port);
      serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

      if (connect(clientSocket, (struct sockaddr *)&serverAddr,
            sizeof(serverAddr)) < 0) {
        logMessage("Connessione fallita");
        return false;
      }

      isConnected = true;
      return true;
    }

    void start() {
      std::string input;
      while (true) {
        showMenu();
        std::getline(std::cin, input);

        if (input == "1") {
          if (!isConnected && !connectToServer()) {
            std::cout << "Impossibile connettersi al server.\nPremi invio per "
              "continuare...";
            std::cin.get();
            continue;
          }

          if (!secureSend("START")) {
            std::cout
              << "Errore nell'avvio del gioco.\nPremi invio per continuare...";
            std::cin.get();
            continue;
          }

          setNickname();
          if (selectTheme()) {
            playQuiz();
            logMessage("Sessione di gioco terminata");
            std::string bC;
            secureReceive(bC);
            if (bC == "BOTH_QUIZZES_COMPLETED") {
              std::string byeMsg = "**************************\n"
                "Hai già completato il quiz su questo tema.\n"
                "***********************************************\n"
                "Premi invio per terminare la sessione..."
                "\n**********************************************\n";
                std::cout << byeMsg;
              std::cin.get();
            }
          }
        } else if (input == "2") {
          std::cout << "Arrivederci!\n";
          break;
        } else {
          std::cout << "Scelta non valida.\nPremi invio per continuare...";
          std::cin.get();
        }
      }

      if (isConnected) {
        close(clientSocket);
      }
    }
};

int main(int argc, char *argv[]) {
  logMessage("------------------------------ CLIENT START -----------------------------\n");
  if (argc != 2) {
    std::cerr << "Uso: " << argv[0] << " <porta>\n";
    return 1;
  }

  int port = std::atoi(argv[1]);
  if (port <= 0 || port > 65535) {
    std::cerr << "Numero di porta non valido\n";
    return 1;
  }

  TriviaClient client(port);
  client.start();

  return 0;
}
