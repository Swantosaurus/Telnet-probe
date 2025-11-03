#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>

#define BUFFER_SIZE 1024

// https://datatracker.ietf.org/doc/html/rfc854
#define WILL char(251)
#define WONT char(252)
#define DO char(253)
#define DONT char(254)
#define IAC char(255)

// OPTIONS HERE
//  https://www.iana.org/assignments/telnet-options/telnet-options.xhtml

// PLATFORM IMPLEMENT HERE
// Platform integraction functions to implement

void LogInfo(const char* msg) {
  std::cout << "[INFO]" << msg << std::endl;
}

void LogError(const char* msg) {
  std::cout << "[ERROR]" << msg << std::endl;
}

// Optional functions using streams
void LogInfo(std::ostringstream& os) {
  LogInfo(os.str().c_str());
}

void LogError(std::ostringstream& os) {
  LogError(os.str().c_str());
}

class TelnetHandler {
  int _port = -1;
  int _sock = -1;

  // always the basic request contains <IAC , (one of DO WILL), telnet-option>
  // messages ll be longer only if we want to handle specific option
  // since we deny ignore all messages ll be only 3 bytes
  // THEREFORE
  // always we will get 3 bytes
  // 1. IAC
  // 2. DO | DONT | WILL | WONT
  // 3. OPTIONS see the link above but we wont need it here
  //
  // we will ignroe and deny all server requsts until server gives up and setup
  // basic backup default config
  void handleRequest(const char* msg_bytes,
                     char* output_buffer,
                     int& output_buffer_end) {
    char pos0 = msg_bytes[0];
    char pos1 = msg_bytes[1];
    char pos2 = msg_bytes[2];

    if (pos0 != IAC) {
      LogError("not starting with msg sync");
      return;
    }
    switch (pos2) {  // do not actually need anything here server will
                     // eventually give up
      default:
        if (pos1 == DO) {
          output_buffer[output_buffer_end++] = IAC;
          output_buffer[output_buffer_end++] = WONT;
          output_buffer[output_buffer_end++] = pos2;
        } else if (pos1 == WILL) {
          output_buffer[output_buffer_end++] = IAC;
          output_buffer[output_buffer_end++] = DONT;
          output_buffer[output_buffer_end++] = pos2;
        } else {
          LogError("what is tis");
        }
    }
  }

  // Reads recieved header and returns response data for Telnet in
  // output_buffer put buffer u recieved from Telnet here if it starts with IAC
  bool processTelnetHeader(const char* recieved,
                           const int& recieved_len,
                           char* output_buffer,
                           int& output_buffer_end) {
    if (recieved_len % 3 != 0) {
      LogError("Unsupported header");
      return false;
    }
    output_buffer_end = 0;
    const char* ptr = recieved;
    for (; ptr < recieved + recieved_len; ptr += 3) {
      if (*ptr == IAC) {  // start of message
        handleRequest(ptr, output_buffer, output_buffer_end);
      }
    }
    return true;
  }

  void printBytes(const char* bytes, const int& bytes_len) {
    std::ostringstream os;
    os << "\n";
    for (int i = 0; i < bytes_len; ++i) {
      // Print each byte as two hex digits
      os << std::setw(3) << std::setfill('0')
         << (static_cast<unsigned int>(static_cast<unsigned char>(bytes[i])))
         << " ";
      if ((i + 1) % 3 == 0)
        os << "\n";  // optional: new line every 16 bytes
    }
    LogInfo(os);
  }

  bool handleTelnetHeader(const char* buffer,
                          const int& bytes,
                          char* bufferOut,
                          int& bufferOutEnd) {
    std::ostringstream os;
    os << "HEADER READ" << bytes << "Bytes :";
    LogInfo(os);

    printBytes(buffer, bytes);

    processTelnetHeader(buffer, bytes, bufferOut, bufferOutEnd);

    std::ostringstream os2;
    os2 << "Sending " << bufferOutEnd << " bytes";
    LogInfo(os2);

    printBytes(bufferOut, bufferOutEnd);
    if (!telnetSend(bufferOut, bufferOutEnd)) {
      LogError("failed to send buffer");
      return false;
    }
    return true;
  }

  bool handleTelnetMessage(const char* cmd,
                           char* buffer,
                           int& bytes,
                           char* bufferOut,
                           int& bufferOutEnd,
                           bool isLoggedIn) {
    buffer[bytes] = '\0';
    std::ostringstream os;
    os << "Recieved string message" << "\n";
    os << buffer;

    LogInfo(os);

    telnetSend(cmd, strlen(cmd));

    // if we are logged in it starts repeating our cmd so we need
    // read once to read command we send and once to read message
    // this is just to read and skip the command repeat
    if (isLoggedIn) {
      std::ostringstream os2;
      os2 << "Reading command repeat";
      LogInfo(os2);
      if (!telnetRecv(buffer, BUFFER_SIZE, bytes)) {
        return false;
      }
    }
    return true;
  }

  bool isHeader(const char* buffer) { return *buffer == IAC; }

  bool _executeCommands(const char** commands, const int commandCnt) {
    const char** commandToSend = commands;
    char buffer[BUFFER_SIZE];         // read in buffer
    char bufferOut[BUFFER_SIZE + 1];  // output buffer for headers
    int bufferOutEnd = 0;  // end of the buffer we write the headers to
    int bytes;             // read bytes count

    // Communication
    for (int i = 0;; i++) {
      std::ostringstream os;
      os << "READING " << i << "th message";
      LogInfo(os);
      if (!telnetRecv(buffer, BUFFER_SIZE, bytes)) {
        return false;
      }
      if (isHeader(buffer)) {
        std::ostringstream os2;
        os2 << "found HEADER";
        LogInfo(os2);
        if (!handleTelnetHeader(buffer, bytes, bufferOut, bufferOutEnd)) {
          return false;
        }
      } else {
        if (commandToSend >= commands + commandCnt)
          break;
        bool isLoggedIn =
            commandToSend >= commands + 1;  // do this on password sending
        if (!handleTelnetMessage(*commandToSend, buffer, bytes, bufferOut,
                                 bufferOutEnd, isLoggedIn)) {
          return false;
        }

        commandToSend++;
      }
    }

    // Print last message recieved
    buffer[bytes] = '\0';
    std::ostringstream os;
    os << "recieved last message: \n" << buffer;

    LogInfo(os);
    return true;
  }

 public:
  bool executeCommands(const char** commands, const int commandCnt) {
    return _executeCommands(commands, commandCnt);
  }

  // WARN this has to be set for given platform
  bool telnetInit(const char* ip, const int port) {
    _port = port;
    _sock = socket(AF_INET, SOCK_STREAM, 0);

    if (_sock < 0) {
      LogError("socket");
      return false;
    }

    // Server address
    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serverAddr.sin_addr) <= 0) {
      LogError("inet_pton");
      return false;
    }

    // Connect to Telnet server
    if (connect(_sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
      LogError("connect");
      return false;
    }

    return true;
  }

  bool telnetSend(const char* buffer, const int& length) {
    if (_sock < 0) {
      LogError("Socket not specified before sending message");
      return false;
    }
    std::ostringstream os;
    int totalSent = 0;

    while (totalSent < length) {
      int sent = send(_sock, buffer + totalSent, length - totalSent, 0);
      if (sent < 0) {
        LogError("send");
        return false;
      }
      totalSent += sent;
    }

    os << "Sent " << totalSent << " bytes:\n" << buffer;
    LogInfo(os);
    return true;
  }

  bool telnetRecv(char* buffer, const int& bufferSize, int& bytesRecieved) {
    if (_sock < 0) {
      LogError("Socket not specified before receieving message");
      return false;
    }
    bytesRecieved = recv(_sock, buffer, bufferSize, 0);
    if (bytesRecieved <= 0) {
      LogError("Failed to receive message");
      return false;
    }
    return true;
  }

  void telnetClose() {
    close(_sock);
    _port = -1;
    _sock = -1;
  }
};

int main() {
  // Telnet address
  const char* serverIp = "192.168.50.189";
  const int serverPort = 23;

  // Telnet credentials
  const char* username = "root\r\n";  //
  const char* password = "123\r\n";

  const char* commands[3] = {// if u get first password give placeholder there
                             // might be hanging session from previous return
                             // password,
                             username, password, "cat secret\r\n"};
  const int cmdCnt = sizeof(commands) / sizeof(commands[0]);

  std::ostringstream os;
  os << "Connecting...";
  LogInfo(os);

  TelnetHandler telnet;
  if (!telnet.telnetInit(serverIp, serverPort)) {
    return 1;
  }

  std::ostringstream os2;
  os2 << "Connected to " << serverIp << ":" << serverPort;
  LogInfo(os2);

  telnet.executeCommands(commands, cmdCnt);

  telnet.telnetClose();

  return 0;
}
