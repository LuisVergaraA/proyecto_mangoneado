// vision.c
// Genera N posiciones aleatorias de mangos en la caja (lado Z) y las envía a robots via TCP.
// Uso: ./vision <host> <port> <N> <Z_cm> <seed>
// Ej:  ./vision 127.0.0.1 9000 12 30 1234

#include "platform.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv){
    if(argc < 6){
        fprintf(stderr, "Uso: %s <host> <port> <N> <Z_cm> <seed>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    int N = atoi(argv[3]);
    double Z = atof(argv[4]);
    unsigned seed = (unsigned)atoi(argv[5]);

    if(N <= 0 || Z <= 0){
        fprintf(stderr, "N y Z deben ser > 0\n");
        return 1;
    }

    init_sockets();

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == INVALID_SOCKET){
        perror("socket");
        cleanup_sockets();
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if(inet_pton(AF_INET, host, &serv.sin_addr) <= 0){
        fprintf(stderr,"Host inválido: %s\n", host);
        close_socket(sock); cleanup_sockets(); return 1;
    }

    if(connect(sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR){
#ifdef _WIN32
        fprintf(stderr, "connect error: %d\n", WSAGetLastError());
#else
        perror("connect");
#endif
        close_socket(sock); cleanup_sockets(); return 1;
    }

    srand(seed);

    // send header: N and Z
    char line[128];
    snprintf(line, sizeof(line), "%d %.6f\n", N, Z);
#ifdef _WIN32
    send(sock, line, (int)strlen(line), 0);
#else
    send(sock, line, strlen(line), 0);
#endif

    for(int i=0;i<N;i++){
        double x = ((double)rand()/RAND_MAX - 0.5) * Z;
        double y = ((double)rand()/RAND_MAX - 0.5) * Z;
        snprintf(line, sizeof(line), "%.6f %.6f\n", x, y);
#ifdef _WIN32
        send(sock, line, (int)strlen(line), 0);
#else
        send(sock, line, strlen(line), 0);
#endif
    }
#ifdef _WIN32
    send(sock, "END\n", 4, 0);
#else
    send(sock, "END\n", 4, 0);
#endif

    printf("vision: enviado %d mangos a %s:%d (Z=%.2f)\n", N, host, port, Z);

    close_socket(sock);
    cleanup_sockets();
    return 0;
}
