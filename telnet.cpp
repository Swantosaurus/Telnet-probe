#include <WiFi.h>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <sstream>
#include "M5Unified.hpp"

const char* wifiSSID = "";
const char* wifiPass = "";
const char* telnetHost = "192.168.50.189";
#define TELNET_PORT 23

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
  Serial.print("[INFO] ");
  Serial.println(msg);
}

void LogError(const char* msg) {
  Serial.print("[ERROR] ");
  Serial.println(msg);
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
  WiFiClient client;

  // always the basic request contains <IAC , (one of DO WILL), telnet-option>
  // messages will be longer only if we want to handle specific option
  // since we deny ignore all messages will be only 3 bytes
  // THEREFORE
  // always we will get 3 bytes
  // 1. IAC
  // 2. DO | DONT | WILL | WONT
  // 3. OPTIONS see the link above but we wont need it here
  //
  // we will ignore and deny all server requests until server gives up and setup
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
  bool processTelnetHeader(const char**& cmd,
                           char* recieved,
                           int& recieved_len,
                           char* output_buffer,
                           int& output_buffer_end) {
    output_buffer_end = 0;
    char* ptr = recieved;
    int rest;
    for (; ptr < recieved + recieved_len; ptr += 3) {
      if (*ptr == IAC) {  // start of message
        handleRequest(ptr, output_buffer, output_buffer_end);
      } else {
        // not a header anymore
        rest = recieved_len - (ptr - recieved) - 1;
        std::ostringstream os;
        os << "eating rest of size: " << rest;
        LogInfo(os);

        return true;  // handleTelnetMessage(cmd, ptr, rest, output_buffer,
                      //                     output_buffer_end, false);
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

  bool handleTelnetHeader(const char**& cmd,
                          char* buffer,
                          int& bytes,
                          char* bufferOut,
                          int& bufferOutEnd) {
    std::ostringstream os;
    os << "HEADER READ " << bytes << " Bytes :";
    LogInfo(os);

    printBytes(buffer, bytes);

    if (!processTelnetHeader(cmd, buffer, bytes, bufferOut, bufferOutEnd)) {
      LogError("Failed to process telnet header");
      return false;
    }

    std::ostringstream os2;
    os2 << "Sending " << bufferOutEnd << " bytes";
    LogInfo(os2);
    printBytes(bufferOut, bufferOutEnd);
    if (!telnetSend(bufferOut, bufferOutEnd)) {
      LogError("failed to send buffer");
      return false;
    }
    if (bytes != bufferOutEnd) {
      // LogInfo("SKIPPING SENDING HEADER IT CONTAINS DATA");
      if (!handleTelnetMessage(cmd, buffer + bufferOutEnd, bytes, bufferOut,
                               bufferOutEnd, false)) {
        return false;
      }
    }
    return true;
  }

  bool handleTelnetMessage(const char**& cmd,
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

    char* cpyCmd = new char[strlen(*cmd) + 1];
    strcpy(cpyCmd, *cmd);
    cmd++;

    if (!telnetSend(cpyCmd, strlen(cpyCmd))) {
      delete[] cpyCmd;
      return false;
    }

    // if we are logged in it starts repeating our cmd so we need
    // read once to read command we send and once to read message
    // this is just to read and skip the command repeat
    if (isLoggedIn) {
      std::ostringstream os2;
      os2 << "Reading command repeat";
      LogInfo(os2);
      if (!telnetRecv(buffer, BUFFER_SIZE, bytes)) {
        delete[] cpyCmd;
        return false;
      }
    }
    delete[] cpyCmd;
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
        if (!handleTelnetHeader(commandToSend, buffer, bytes, bufferOut,
                                bufferOutEnd)) {
          return false;
        }
      } else {
        if (commandToSend >= commands + commandCnt)
          break;
        bool isLoggedIn = false;
        // commandToSend >= commands + 2;  // do this on password sending
        if (!handleTelnetMessage(commandToSend, buffer, bytes, bufferOut,
                                 bufferOutEnd, isLoggedIn)) {
          return false;
        }
      }
    }

    // Print last message recieved
    buffer[bytes] = '\0';
    std::ostringstream os;
    os << "Recieved last message: \n" << buffer;

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
    if (!client.connect(ip, port)) {
      std::ostringstream os;
      os << "Error connecting telnet" << telnetHost << ":" << port;
      LogError(os);
    }

    std::ostringstream os;
    os << "Connected to telnet server";
    LogInfo(os);

    return true;
  }

  bool telnetSend(char* buffer, const int& length) {
    int sent = client.write(buffer, length);
    if (sent <= 0) {
      LogError("failed to send data");
      return false;
    }
    buffer[length] = '\0';
    std::ostringstream os;
    os << "Sending data:\n" << buffer;
    LogInfo(os);
    return true;
  }

  bool telnetRecv(char* buffer, const int& bufferSize, int& bytesRecieved) {
    bytesRecieved = client.readBytes(buffer, bufferSize);
    if (bytesRecieved <= 0) {
      LogError("failed to read buffer");
      return false;
    }
    buffer[bytesRecieved] = '\0';
    std::ostringstream os;
    os << "Recieved new message:\n";
    os << buffer;
    LogInfo(os);

    return true;
  }

  void telnetClose() { client.~WiFiClient(); }
};

void setup() {
  M5.begin(M5.config());
  M5.delay(200);

  // Telnet credentials
  const char* username = "root\r\n";  //
  const char* password = "123\r\n";

  const char* commands[3] = {// if u get first password give placeholder there
                             // might be hanging session from previous return
                             // password,
                             username, password, "cat secret\r\n"};
  const int cmdCnt = sizeof(commands) / sizeof(commands[0]);

  Serial.begin(9600);

  M5.Display.setTextSize(4);

  WiFi.begin(wifiSSID, wifiPass);

  Serial.print("Connecting ");
  Serial.print(wifiSSID);
  Serial.print(" ");
  Serial.println(wifiPass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("Status: ");
    Serial.println(WiFi.status());
    M5.Display.print(".");
  }

  M5.Display.print("WL_CONNECTED");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  TelnetHandler telnet;
  telnet.telnetInit(telnetHost, TELNET_PORT);
  telnet.executeCommands(commands, cmdCnt);
  telnet.telnetClose();
}

void loop() {}
