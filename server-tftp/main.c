/*su principal responsabilidad es la configuración inicial y el manejo del bucle principal del servidor*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>

#define PORT 8888
#define IP   "127.0.0.1"
#define INITIAL_TIMEOUT_SEC 1

static int fd;
struct sockaddr_in src_addr;
socklen_t src_addr_len;

void handler(int signal);
void processRequest(int fd, struct sockaddr_in addr);

void handler(int signal) {
    close(fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    struct sockaddr_in addr;
    signal(SIGTERM, handler);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    if (argc == 3) {
        addr.sin_port = htons((uint16_t)atoi(argv[2]));
        inet_aton(argv[1], &(addr.sin_addr));
    } else {
        addr.sin_port = htons(PORT);
        inet_aton(IP, &(addr.sin_addr));
    }

    int optval = 1;
    int optname = SO_REUSEADDR | SO_REUSEPORT;
    if (setsockopt(fd, SOL_SOCKET, optname, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    struct timeval timeout;
    timeout.tv_sec = INITIAL_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        close(fd);
        exit(EXIT_FAILURE);
    }

    int b = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (b == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    while (1) {
        processRequest(fd, addr);
    }

    close(fd);
    return 0;
}
