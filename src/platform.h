#ifndef PLATFORM_H
#define PLATFORM_H

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #include <unistd.h>
#endif

#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef SOCKET socket_t;
  #define close_socket(s) closesocket(s)
  static inline void init_sockets() { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
  static inline void cleanup_sockets() { WSACleanup(); }
  static inline void sleep_ms(int ms) { Sleep(ms); }
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <fcntl.h>
  typedef int socket_t;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
  #define close_socket(s) close(s)
  static inline void init_sockets() {}
  static inline void cleanup_sockets() {}
  static inline void sleep_ms(int ms) { usleep((ms)*1000); }
#endif

#endif // PLATFORM_H