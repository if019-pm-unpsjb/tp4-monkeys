#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#define BUFSIZE 100
#define MAX_RETRIES 5

extern struct sockaddr_in src_addr;
extern socklen_t src_addr_len;

void buildDataPackage(unsigned char *response, unsigned char *fileBuffer, size_t bytesRead, unsigned short blockN);
void sendErrorPackage(int fd, unsigned char *message);

void processRequest(int fd, struct sockaddr_in addr) {
    unsigned char buf[BUFSIZE];
    int received = 0;
    FILE *file;
    printf("Escuchando en %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    memset(&src_addr, 0, sizeof(struct sockaddr_in));
    src_addr_len = sizeof(struct sockaddr_in);

    int retries = 0;
    received = 0;
    char filename[100] = "";
    while (received == 0) {
        ssize_t n = recvfrom(fd, (char*)&buf, BUFSIZE, 0, (struct sockaddr*)&src_addr, &src_addr_len);
        if (n == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                retries++;
                continue;
            } else {
                perror("Error en recvfrom");
                exit(EXIT_FAILURE);
            }
        } else {
            char opcode[2] = "";
            opcode[0] = buf[0];
            opcode[1] = buf[1];

            int i = 2;
            int filenameSize = 0;
            while (buf[i] != '\0') {
                filename[i - 2] = buf[i];
                i++;
                filenameSize++;
            }
            if (opcode[1] != '1' && opcode[1] != '4') {
                printf("Operation not implemented %s\n", buf);
                return;
            } else if (opcode[1] != '4') {
                received = 1;
            }

            file = fopen(filename, "r");
            if (file == NULL) {
                printf("Error %s\n", filename);
                sendErrorPackage(fd, (unsigned char*)"Error: el archivo solicitado no existe o no tenés permisos.\n");
                memset(filename, 0, sizeof(filename));
                received = 0;
            }
        }
    }

    size_t bytesRead;
    unsigned char response[516];
    unsigned short blockN = 1;
    unsigned short ackBlock;
    unsigned char fileBuffer[512] = {0};
    int done = 0;
    while (done == 0) {
        memset(fileBuffer, 0, sizeof(fileBuffer));
        memset(response, 0, sizeof(response));
        bytesRead = fread(fileBuffer, 1, sizeof(fileBuffer), file);
        buildDataPackage(response, fileBuffer, bytesRead, blockN);

        received = 0;
        retries = 0;
        while (received == 0 && retries <= MAX_RETRIES) {
            int n = sendto(fd, (char*)&response, bytesRead + 4, 0, (struct sockaddr*)&src_addr, sizeof(src_addr));
            if (n == -1) {
                perror("Error al enviar");
                exit(1);
            }

            unsigned char ackBuf[4];
            n = recvfrom(fd, (char*)&ackBuf, 4, 0, (struct sockaddr*)&src_addr, &src_addr_len);
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
                if (ackBlock != blockN || ackBuf[1] != '4') {
                    if (ackBlock < blockN) {
                        retries = 0;
                    } else {
                        printf("Error en el acknowledge. Bloque: %d. Opcode: %c%c. %s", ackBlock, ackBuf[0], ackBuf[1], ackBuf);
                        perror("Error");
                        exit(1);
                    }
                } else {
                    received = 1;
                }
            }
        }
        blockN++;
        if (bytesRead < 512) {
            printf("Dejo de escucharte!\n");
            done = 1;
            break;
        }
    }

    if (retries >= MAX_RETRIES) {
        printf("Máximo número de reintentos alcanzado. No se puede enviar el archivo.\n");
    }
}
