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
#define BUFSIZE 100
#define INITIAL_TIMEOUT_SEC 10
#define MAX_RETRIES 5

static int fd;

struct tftp_packet {
    short opcode;
    char filename[25];
    char eof1;
    char mode[25];
    char eof2;
};

void handler(int signal)
{
    close(fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[])
{
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
        addr.sin_port = htons((uint16_t) atoi(argv[2]));
        inet_aton(argv[1], &(addr.sin_addr));
    } else {
        addr.sin_port = htons(PORT);
        inet_aton(IP, &(addr.sin_addr));
    }

    int optval = 1;
    int optname = SO_REUSEADDR | SO_REUSEPORT;
    if(setsockopt(fd, SOL_SOCKET, optname, &optval, sizeof(optval)) == -1) {
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

    int b = bind(fd, (struct sockaddr*) &addr, sizeof(addr));
    if (b == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Escuchando en %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    unsigned char buf[BUFSIZE];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;

    for (;;) {
        memset(&src_addr, 0, sizeof(struct sockaddr_in));
        src_addr_len = sizeof(struct sockaddr_in);

        int retries = 0;
        while (retries < MAX_RETRIES) {
            ssize_t n = recvfrom(fd, (char *) &buf, BUFSIZE, 0, (struct sockaddr*) &src_addr, &src_addr_len);

            if (n == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    printf("recvfrom timed out, retrying...\n");
                    retries++;
                    continue;
                } else {
                    perror("Error en recvfrom");
                    exit(EXIT_FAILURE);
                }
            }

            // Obtengo los datos del paquete
            char opcode[2] = "";
            opcode[0] = (buf[0]);
            opcode[1] = (buf[1]);

            char filename[100] = "";
            int i = 2;
            int filenameSize = 0;
            while (buf[i] != '\0') {
                filename[i - 2] = buf[i];
                i++;
                filenameSize++;
            }

            if (opcode[1] != '1') {
                perror("Operation not implemented");
                return 1;
            }

            FILE *file;
            file = fopen(filename, "r");
            if (file == NULL) {
                perror("Error al abrir el archivo");
                return EXIT_FAILURE;
            }

            size_t bytesRead;

            unsigned char response[516];
            unsigned short blockN = 1;
            unsigned short ackBlock;
            unsigned char fileBuffer[512] = {0};
            while (1) {
                memset(fileBuffer, 0, sizeof(fileBuffer));
                memset(response, 0, sizeof(response));
                bytesRead = fread(fileBuffer, 1, sizeof(fileBuffer), file);
                response[0] = '0';
                response[1] = '3';
                response[2] = (unsigned char)((blockN >> 8) & 0xFF);
                response[3] = (unsigned char)(blockN & 0xFF);
                
                memcpy(response + 4, fileBuffer, bytesRead);
                 
                int n = sendto(fd, (char *) &response, bytesRead + 4, 0, (struct sockaddr*) &src_addr, sizeof(src_addr));
                if (n == -1) {
                    perror("Error al enviar");
                    exit(1);
                }

                unsigned char ackBuf[4];
                n = recvfrom(fd, (char *) &ackBuf, 4, 0, (struct sockaddr*) &src_addr, &src_addr_len);
                if (n == -1) {
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        printf("recvfrom timed out\n");
                    } else {
                        perror("Error en recvfrom");
                    }
                    exit(1);
                }

                ackBlock = (short)((ackBuf[2] << 8) | ackBuf[3]); 

                if (ackBlock != blockN || ackBuf[1] != '4') {
                    printf("Error en el acknowledge. Bloque: %d. Opcode: %c%c.", ackBlock, ackBuf[0], ackBuf[1]);
                    perror("Error");
                    exit(1);
                } 

                if (bytesRead < 512) {
                    printf("Dejo de escucharte!\n");
                    break;
                }
                blockN++;
            }
            break;
        }

        if (retries >= MAX_RETRIES) {
            printf("Máximo número de reintentos alcanzado. No se puede enviar el archivo.\n");
            break;
        }
    }

    close(fd);
    exit(EXIT_SUCCESS);
}
