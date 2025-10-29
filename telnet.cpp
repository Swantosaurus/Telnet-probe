#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <ostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// https://datatracker.ietf.org/doc/html/rfc854
#define WILL char(251)
#define WONT char(252)
#define DO char(253)
#define DONT char(254)
#define IAC char(255)

// https://www.iana.org/assignments/telnet-options/telnet-options.xhtml
// here u can find the options

void handleRequest(const char *msg_bytes, char *output_buffer,
                   int &output_buffer_end) {

  char pos0 = msg_bytes[0];
  char pos1 = msg_bytes[1];
  char pos2 = msg_bytes[2];

  if (pos0 != IAC) {
    perror("not starting with msg sync");
    return;
  }
  switch (pos2) { // do not actually need anything here server will eventually
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

void readHeader(const char *recieved, const int &recieved_len,
                char *output_buffer, int &output_buffer_end) {
  const char *ptr = recieved;
  for (; ptr < recieved + recieved_len * sizeof(char); ptr += sizeof(char)) {
    if (*ptr == IAC) { // start of message
      handleRequest(ptr, output_buffer, output_buffer_end);
    }
  }
}

void printBytes(const char *bytes, const int &bytes_len) {
  for (int i = 0; i < bytes_len; ++i) {
    // Print each byte as two hex digits
    std::cout << std::setw(2) << std::setfill('0')
              << (static_cast<unsigned int>(
                     static_cast<unsigned char>(bytes[i])))
              << " ";
    if ((i + 1) % 3 == 0)
      std::cout << "\n"; // optional: new line every 16 bytes
  }
}

bool sendBuffer(int sock, const char *buffer, int length) {
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

int main() {
  const char *server_ip = "192.168.50.189"; // Windows host IP
  const int server_port = 23;

  // Create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  // Server address
  struct sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    return 1;
  }

  // Connect to Telnet server
  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    return 1;
  }

  std::cout << "Connected to " << server_ip << ":" << server_port << std::endl;

  char buffer[1024];
  char bufferOut[1024];
  int bufferOutEnd = 0;

  // HEADER 01
  int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recive header");
    return 1;
  }
  std::cout << "Received " << bytes << " bytes:\n";
  printBytes(buffer, bytes);

  readHeader(buffer, bytes, bufferOut, bufferOutEnd);

  std::cout << "Sending" << bufferOutEnd << "bytes\n";
  printBytes(bufferOut, bufferOutEnd);
  if (!sendBuffer(sock, bufferOut, bufferOutEnd)) {
    perror("failed to send buffer");
    return 2;
  }

  // HEADER 02
  bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recieve next message");
    return 3;
  }

  buffer[bytes] = '\0';
  std::cout << "revieved new message: " << std::endl;

  std::cout << buffer << std::endl;
  printBytes(buffer, bytes);

  bufferOutEnd = 0;
  readHeader(buffer, bytes, bufferOut, bufferOutEnd);

  if (!sendBuffer(sock, bufferOut, bufferOutEnd)) {
    perror("fail");
    return 4;
  }

  // HEADER 03
  bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recieve next message (3)");
    return 5;
  }

  buffer[bytes] = '\0';
  std::cout << "revieved new message: " << std::endl;

  std::cout << buffer << std::endl;
  printBytes(buffer, bytes);

  bufferOutEnd = 0;
  readHeader(buffer, bytes, bufferOut, bufferOutEnd);

  if (!sendBuffer(sock, bufferOut, bufferOutEnd)) {
    perror("fail");
    return 6;
  }

  // login prompt
  bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recieve next message (4)");
    return 7;
  }

  buffer[bytes] = '\0';
  std::cout << "revieved new message: " << std::endl;
  std::cout << buffer << std::endl;

  const char *username = "root\r\n";
  const char *password = "123\r\n";

  int sent = send(sock, username, strlen(username), 0);
  if (sent < 0) {
    perror("send");
    return 8;
  }

  std::cout << "Uname Send" << std::endl;

  // Some more header after login send
  bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recieve next message (4)");
    return 9;
  }
  buffer[bytes] = '\0';
  std::cout << "revieved new message: " << std::endl;
  std::cout << buffer << std::endl;
  printBytes(buffer, bytes);

  bufferOutEnd = 0;
  readHeader(buffer, bytes, bufferOut, bufferOutEnd);

  if (!sendBuffer(sock, bufferOut, bufferOutEnd)) {
    perror("fail");
    return 12;
  }

  std::cout << "sned sm header again" << std::endl;

  bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0) {
    perror("failed to recieve next message (4)");
    return 9;
  }
  buffer[bytes] = '\0';
  std::cout << "revieved new message: " << std::endl;
  std::cout << buffer << std::endl;

  // Password
  sent = send(sock, password, strlen(password), 0);
  if (sent < 0) {
    perror("send");
    return 8;
  }

  std::cout << "password Send" << std::endl;

  for (int i = 0; i < 2;
       i++) { // it repeats the last command so thats why teres 2
    bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    std::cout << "Bytes:" << bytes << std::endl;
    if (bytes <= 0) {
      break;
    }
    buffer[bytes] = '\0';
    std::cout << buffer;
  }
  std::cout << std::endl;

  // Try to execute smth
  const char *pwd = "cat secret\r\n";

  sent = send(sock, pwd, strlen(pwd), 0);
  if (sent < 0) {
    perror("send");
    return 8;
  }

  std::cout << "cat secret Send" << std::endl;

  for (int i = 0; i < 2; i++) {
    bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    std::cout << "Bytes:" << bytes << std::endl;
    if (bytes <= 0) {
      break;
    }
    buffer[bytes] = '\0';
    // std::cout << "revieved new message: " << std::endl;
    std::cout << buffer;
  }

  close(sock);
  return 0;
}
