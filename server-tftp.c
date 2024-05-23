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
#define INITIAL_TIMEOUT_SEC 0
#define MAX_RETRIES 5

static int fd;

struct tftp_packet {
    short opcode;
    char filename[25];
    char eof1;
    char mode[25];
    char eof2;
};

void sendDataAndWait();
void buildDataPackage(unsigned char * response, unsigned char * fileBuffer, size_t bytesRead, unsigned short blockN);
void sendErrorPackage(unsigned char * message);
void handler(int signal)
{
    close(fd);
    exit(EXIT_SUCCESS);
}

struct sockaddr_in src_addr;
socklen_t src_addr_len;
unsigned char response[516];
unsigned short blockN = 1;
unsigned short ackBlock;
int received = 0;
int retries = 0;
size_t bytesRead;

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

    
    unsigned char buf[BUFSIZE];

    received = 0;

    FILE *file;
    for (;;) {
        printf("Escuchando en %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        memset(&src_addr, 0, sizeof(struct sockaddr_in));
        src_addr_len = sizeof(struct sockaddr_in);

        blockN = 1;
        retries = 0;
        received = 0;
        char filename[100] = "";
        while (received == 0) {
            ssize_t n = recvfrom(fd, (char *) &buf, BUFSIZE, 0, (struct sockaddr*) &src_addr, &src_addr_len);

            if (n == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    retries++;
                    continue;
                } else {
                    perror("Error en recvfrom");
                    exit(EXIT_FAILURE);
                }
            } else {
                // Obtengo los datos del paquete
                char opcode[2] = "";
                opcode[0] = (buf[0]);
                opcode[1] = (buf[1]);

                int i = 2;
                int filenameSize = 0;
                while (buf[i] != '\0') {
                    filename[i - 2] = buf[i];
                    i++;
                    filenameSize++;
                }
                if (opcode[1] != 1 && opcode[1] != 4) {
                    printf("Operation not implemented %d\n", opcode[1]);
                    return 1;
                } else if (opcode[1] != 4) {
                    received = 1;
                }

                file = fopen(filename, "r");
                if (file == NULL) {
                    printf("Error %s\n", filename);
                    sendErrorPackage((unsigned char *)"Error: el archivo solicitado no existe o no tenés permisos.\n");
                    memset(filename, 0, sizeof(filename));
                    received = 0;
                }

            }
        }

        unsigned char fileBuffer[512] = {0};
        int done = 0;
        while (done == 0) {
            memset(fileBuffer, 0, sizeof(fileBuffer));
            memset(response, 0, sizeof(response));
            bytesRead = fread(fileBuffer, 1, sizeof(fileBuffer), file);
            buildDataPackage(response, fileBuffer, bytesRead, blockN);
            sendDataAndWait();
            
            blockN++;
            if (bytesRead < 512) {
                printf("Dejo de escucharte!\n");
                done = 1;
                break;
            }
        }
        if (retries >= MAX_RETRIES) {
            printf("Máximo número de reintentos alcanzado. No se puede enviar el archivo.\n");
            break;
        }
    }

    close(fd);
    exit(EXIT_SUCCESS);
}

void buildDataPackage(unsigned char * response, unsigned char * fileBuffer, size_t bytesRead, unsigned short blockN) {
    response[0] = 0;
    response[1] = 3;
    response[2] = (unsigned char)((blockN >> 8) & 0xFF);
    response[3] = (unsigned char)(blockN & 0xFF);
    memcpy(response + 4, fileBuffer, bytesRead);
}

void sendErrorPackage(unsigned char * message) {
    unsigned char response[100];
    response[0] = 0;
    response[1] = 5;
    memcpy(response + 2, message, strlen((char *)message));

    sendto(fd, (char *) &response, 2 + strlen((char *)message), 0, (struct sockaddr*) &src_addr, sizeof(src_addr));
}


void sendDataAndWait() {
    received = 0;
    retries = 0;
    while (received == 0 && retries <= MAX_RETRIES) {
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
                retries++;
                if (retries == MAX_RETRIES) {
                    perror("Max retries reached");
                    exit(1);
                }
            } else {
                perror("Error en recvfrom");
            }
        } else {
            printf("Bloque %d recibido\n", (short)((ackBuf[2] << 8) | ackBuf[3]));
            ackBlock = (short)((ackBuf[2] << 8) | ackBuf[3]); 
            if (ackBlock != blockN || ackBuf[1] != 4) {
                if (ackBlock < blockN) {
                    retries = 0;
                } else {
                    printf("%X | %X\n", ackBuf[2], ackBuf[3]);
                    printf("Error en el acknowledge. Bloque: %d. Opcode: %c%c. %s", ackBlock, ackBuf[0], ackBuf[1], ackBuf);
                    perror("Error");
                    exit(1);
                }
            } else {
                received = 1;
            }
        }                
    }
}