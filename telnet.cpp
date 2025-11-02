#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ostream>

#define BUFFER_SIZE 1024

// https://datatracker.ietf.org/doc/html/rfc854
#define WILL char(251)
#define WONT char(252)
#define DO char(253)
#define DONT char(254)
#define IAC char(255)

// OPTIONS HERE
//  https://www.iana.org/assignments/telnet-options/telnet-options.xhtml
//  here u can find the options

// always the basic request contains <IAC , (one of DO WILL), telnet-option>
// messages ll be longer only if we want to handle specific option
// since we deny ignore all all messages ll be only 3 bytes
// THEREFORE
// always we will get 3 bytes
// 1. IAC
// 2. DO | DONT | WILL | WONT
// 3. OPTIONS see th link above but we wont need it here
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
    perror("not starting with msg sync");
    return;
  }
  switch (pos2) {  // do not actually need anything here server will eventually
                   // give up
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
        perror("what is tis");
      }
  }
}

// Reads threw recieved header and returns response data for Telnet in
// output_buffer put buffer u recieved from Telnet here if it starts with IAC
void processTelnetHeader(const char* recieved,
                         const int& recieved_len,
                         char* output_buffer,
                         int& output_buffer_end) {
  const char* ptr = recieved;
  for (; ptr < recieved + recieved_len * sizeof(char); ptr += sizeof(char)) {
    if (*ptr == IAC) {  // start of message
      handleRequest(ptr, output_buffer, output_buffer_end);
    }
  }
}

void printBytes(const char* bytes, const int& bytes_len) {
  for (int i = 0; i < bytes_len; ++i) {
    // Print each byte as two hex digits
    std::cout << std::setw(3) << std::setfill('0')
              << (static_cast<unsigned int>(
                     static_cast<unsigned char>(bytes[i])))
              << " ";
    if ((i + 1) % 3 == 0)
      std::cout << "\n";  // optional: new line every 16 bytes
  }
}

bool sendBuffer(int sock, const char* buffer, int length) {
  int totalSent = 0;

  while (totalSent < length) {
    int sent = send(sock, buffer + totalSent, length - totalSent, 0);
    if (sent < 0) {
      perror("send");
      return false;
    }
    totalSent += sent;
  }

  std::cout << "Sent " << totalSent << " bytes." << std::endl;
  return true;
}

bool handleTelnetHeader(const int& sock,
                        const char* buffer,
                        const int& bytes,
                        char* bufferOut,
                        int& bufferOutEnd) {
  std::cout << "HEADER READ " << bytes << " Bytes :" << std::endl;
  printBytes(buffer, bytes);

  processTelnetHeader(buffer, bytes, bufferOut, bufferOutEnd);

  std::cout << "\nSending " << bufferOutEnd << " bytes: \n";
  printBytes(bufferOut, bufferOutEnd);
  if (!sendBuffer(sock, bufferOut, bufferOutEnd)) {
    perror("failed to send buffer");
    return false;
  }
  return true;
}

bool handleTelnetMessage(const char* cmd,
                         const int& sock,
                         char* buffer,
                         int& bytes,
                         char* bufferOut,
                         int& bufferOutEnd,
                         bool isLoggedIn) {
  buffer[bytes] = '\0';
  std::cout << "Recieved string message" << "\n";
  std::cout << buffer << "\n\n";

  std::cout << "Sending: " << cmd << std::endl;
  int sent = send(sock, cmd, strlen(cmd), 0);
  if (sent <= 0) {
    std::cout << "Error sending command " << cmd << std::endl;
    return false;
  }
  // if we are logged in it starts repeating our cmd so we need read it wo
  // printing out once for terminal output
  if (isLoggedIn) {
    bytes = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytes <= 0) {
      perror("failed to read second message");
      return false;
    }
  }
  return true;
}

bool isHeader(const char* buffer) {
  return *buffer == IAC;
}

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
  const char** cmdPtr = commands;
  const int cmdCnt = sizeof(commands) / sizeof(commands[0]);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  // Server address
  struct sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(serverPort);
  if (inet_pton(AF_INET, serverIp, &serverAddr.sin_addr) <= 0) {
    perror("inet_pton");
    return 1;
  }

  // Connect to Telnet server
  if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    perror("connect");
    return 1;
  }

  std::cout << "Connected to " << serverIp << ":" << serverPort << std::endl;

  // this should be enuf but if u got long promtp at start u might struggle
  char buffer[BUFFER_SIZE];     // read in buffer
  char bufferOut[BUFFER_SIZE];  // output buffer for headers
  int bufferOutEnd = 0;         // end of the buffer we write the headers to
  int bytes;                    // read bytes count

  // READ ALL STARTING HEADERS AND RESPOND TO THEM WITH IGNORING
  for (int i = 0;; i++) {
    std::cout << "\n" << "READING " << i << "\n";
    bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    bufferOutEnd = 0;
    if (bytes <= 0) {
      perror("failed to recive");
      return 1;
    }
    if (isHeader(buffer)) {
      if (!handleTelnetHeader(sock, buffer, bytes, bufferOut, bufferOutEnd)) {
        return 2;
      }
    } else {
      if (cmdPtr >= commands + cmdCnt)
        break;
      bool isLoggedIn = cmdPtr >= commands + 1;  // do this on password sending
      if (!handleTelnetMessage(*cmdPtr, sock, buffer, bytes, bufferOut,
                               bufferOutEnd, isLoggedIn)) {
        return 3;
      }

      cmdPtr++;
    }
  }

  // Print last message recieved
  buffer[bytes] = '\0';
  std::cout << "revieved last message: " << std::endl;
  std::cout << buffer << std::endl;

  close(sock);
  return 0;
}
