#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#define BUFFER_SIZE 1024

std::ofstream logFile("client.log", std::ios::out | std::ios::app);
std::mutex playersMutex;

std::string logSeparator = "----------------------------------\n\tCLIENT LOG\n----------------------------------\n";

void logMessage(const std::string &message) {
    std::lock_guard<std::mutex> lock(playersMutex);
    auto now = std::chrono::system_clock::now();
    logFile << "[" << std::chrono::system_clock::to_time_t(now) << "] " 
            << message << std::endl;
}

void clearScreen() {
    std::cout << "\033[2J\033[1;1H";
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
    int clientSocket;
    std::string nickname;
    int theme;
    int port;

    std::string secureInput() {
      std::string input;
      std::getline(std::cin, input);
      if ( input.empty() ) {
        printf("Nessun input rilevato, riprova\n");
        printf("Premi invio per continuare...");
        std::cin.ignore();
        secureInput();
      }
      return input;
    }

    bool secureSend(const std::string &message) {
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

bool secureReceive(std::string &message) {
 char buffer[BUFFER_SIZE] = {0};
    int received = 0;

        int bytes_read = recv(clientSocket, &received, sizeof(received), 0);
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
        bytes_read = recv(clientSocket, buffer, received, 0);
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

    void handleServerDisconnect() {
      printf("Il server ha chiuso la connessione\n");
      logMessage("Server closed the connection");
      close(clientSocket);
      exit(1);
    }

    bool setTheme() {
      logMessage("-------------------- Theme Selection --------------------");
      sTheme();
      std::string input = secureInput();
      try {
        theme = std::stoi(input);
      } catch (std::invalid_argument &e) {
        logMessage("Invalid theme: " + input);
        printf("Input non valido, riprova\n");
        printf("Premi invio per continuare...");
        std::cin.ignore();
        setTheme();
      }
      logMessage("*Selected theme: " + std::to_string(theme));
      if (!secureSend(std::to_string(theme))) {
        printf("Errore nell'invio del tema, prova di nuovo\n");
        printf("Press enter to continue...");
        logMessage("Error sending theme");
        std::cin.ignore();
        setTheme();
      }
      std::string response;
      if (!secureReceive(response)) {
        printf("Errore nella ricezione della risposta\n");
        logMessage("Error receiving response");
        return false;
      }
      if (response == "OK") {
        logMessage("Theme set");
        return true;
      }
      if (response == "INVALID_THEME") {
        printf("Tema non valido, prova di nuovo\n");
        printf("Press enter to continue...");
        logMessage("Invalid theme");
        std::cin.ignore();
        setTheme();
      }
      return true;
    }

    bool setNickname() {
      logMessage("-------------------- Nickname Selection --------------------");
      sNickname();
      nickname = secureInput();
      if (!secureSend(nickname)) { 
        printf("Errore nell'invio del nickname, prova di nuovo\n");
        printf("Press enter to continue...");
        logMessage("Error sending nickname");
        std::cin.ignore();
        setNickname();
      }
      std::string response;
      if (!secureReceive(response)) { 
        printf("Errore nella ricezione della risposta\n");
        logMessage("Error receiving response");
        return false;
      }
      if (response == "OK") { 
        logMessage("Nickname set");
        return true;
      }
      if (response == "NICKNAME_ALREADY_USED") {
        printf("Il nickname e' gia' in uso, prova di nuovo\n");
        printf("Press enter to continue...");
        logMessage("Nickname already used");
        std::cin.ignore();
        setNickname();
      }
      return true;
    }

  public:
    TriviaClient(int serverPort) : clientSocket(0), port(serverPort), theme(0) {
        nickname = "";
    }

    void connectToServer() {
     struct sockaddr_in serverAddr;

        if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("Errore nella creazione del socket\n");
            logMessage("Socket creation error");
            return;
        }

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
            printf("Indirizzo non valido\n");
            logMessage("Invalid address/ Address not supported");
            return;
        }

        if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
            printf("Connessione fallita\n");
            logMessage("Connection Failed");
            return;
        }
    }

    void playQuestions() {
      std::string quesiton;
      while ( true ) {
        /*Start send and receive questions*/
        if (!secureReceive(quesiton)) {
          printf("Errore nella ricezione della domanda\n");
          logMessage("Error receiving question");
          return;
        }
        if (quesiton == "COMPLETED_QUIZ"){
          printf("*******************************\n\tHai completato il quiz\n*******************************\n");
          logMessage("Quiz completed");
          setTheme();
        }
        std::string themeString = (theme == 1) ? "Curiosita' sulla tecnologia" : "Cultura generale";
        printf("\033[2J\033[1;1H");
        printf("\n    Quiz - %s\n**************************************\n%s\n**************************************\n", themeString.c_str(), quesiton.c_str());
        std::string answer;
        answer = secureInput();
        if (!secureSend(answer)) {
          printf("Errore nell'invio della risposta\n");
          logMessage("Error sending answer");
          return;
        }

        if (answer == "show score" || answer == "endquiz") {
          if (answer == "show score") {
            std::string msg = "show score";
            if (!secureSend(msg)) {
              printf("Errore nell'invio del punteggio\n");
              logMessage("Error sending score");
              return;
            }
            std::string scoreboard;
            if (!secureReceive(scoreboard)) {
              printf("Errore nella ricezione del punteggio\n");
              logMessage("Error receiving score");
              return;
            }
            printf("\033[2J\033[1;1H");
            printf("%s\n", scoreboard.c_str());
            continue;
          }

          if (answer == "endquiz") {
            std::string finishMessage;
            if (!secureReceive(finishMessage)) {
              printf("Errore nella ricezione del messaggio di fine quiz\n");
              logMessage("Error receiving finish message");
              return;
            }
            printf("%s\n", finishMessage.c_str());
            printf("Premi invio per continuare...");
            std::cin.ignore();
            break;
          }
        }
        /*Finich send and receive questions*/

        /*Start receive question result*/
        std::string qStatus;
        if (!secureReceive(qStatus)) {
          printf("Errore nella ricezione dello stato della risposta\n");
          logMessage("Error receiving answer status");
          return;
        }
        printf("\033[2J\033[1;1H");
        printf("\n*****************************\n\t%s\n*****************************\n", qStatus.c_str());
        printf("Premi invio per continuare...");
        std::cin.ignore();
        printf("\033[2J\033[H");
      }
    }

    void start() {
      frontPage();
      int choice;
      choice = std::stoi(secureInput());
      if (choice == 1) {
        connectToServer();
        while (!setNickname()) {
          printf("Errore nella scelta del nickname\n");
          logMessage("Error setting nickname");
          return;
        }
        while (!setTheme()) {
          printf("Errore nella scelta del tema\n");
          logMessage("Error setting theme");
          return;
        }
        playQuestions();
      } else if (choice == 2) {
        printf("Arrivederci\n");
        logMessage("Goodbye");
        return;
      } else {
        printf("Scelta non valida\n");
        logMessage("Invalid choice");
        return;
      }
    }
};

int main (int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Uso: " << argv[0] << "<port>" << std::endl;
    return 1;
  }

  logMessage(logSeparator);

  int port = atoi(argv[1]);
  TriviaClient client(port);
  client.start();

  return 0;
}
