#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>

#define PORT 8888
#define IP   "127.0.0.1"
#define BUFSIZE 100
#define BLOCKSIZE 512
#define INITIAL_TIMEOUT_SEC 10
#define MAX_RETRIES 5

static int fd;

void handler(int signal)
{
    close(fd);
    exit(EXIT_SUCCESS);
}

void buildRequestPackage(unsigned char *str, char opcode[2], char filename[100], char mode[100]);
void receiveFile(char *destFilename);
void buildAckPackage(unsigned char *str, unsigned short block);

struct sockaddr_in addr;

int main(int argc, char* argv[])
{
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
        addr.sin_port = htons(0);
        inet_aton(IP, &(addr.sin_addr));
    }

    int optval = 1;
    int optname = SO_REUSEADDR | SO_REUSEPORT;
    if (setsockopt(fd, SOL_SOCKET, optname, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    int b = bind(fd, (struct sockaddr*) &addr, sizeof(addr));
    if (b == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    addr.sin_port = htons(PORT);

    printf("Mandando a %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    char opcode[2] = "01";
    char filename[100] = "test.txt";
    char mode[100] = "NETASCII";

    unsigned char str[202];
    buildRequestPackage(str, opcode, filename, mode);

    if (opcode[1] != '1') {
        perror("Operación no implementada");
        exit(1);
    }
    
    char destFilename[100] = "test2.txt";
    // sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));
    receiveFile(destFilename);
    
    close(fd);
    exit(EXIT_SUCCESS);
}

void buildRequestPackage(unsigned char *str, char opcode[2], char filename[100], char mode[100]) {
    str[0] = opcode[0];
    str[1] = opcode[1];
    int i = 0;
    int size = 3;
    while (filename[i] != '\0') {
        str[i + 2] = filename[i];
        i++;
        size++;
    }
    str[i + 2] = '\0';
    size++;

    i = 0;
    while (mode[i] != '\0') {
        str[size] = mode[i];
        i++;
        size++;
    }
    str[size] = '\0';
}

void receiveFile(char *destFilename) {
    FILE *file = fopen(destFilename, "w");
    if (file == NULL) {
        perror("Error al abrir el archivo para escribir");
        exit(EXIT_FAILURE);
    }

    unsigned short expectedBlock = 1;
    unsigned char buf[516];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    int retries;

    while (1) {
        memset(buf, 0, sizeof(buf));
        retries = 0;

        while (retries < MAX_RETRIES) {
            ssize_t n = recvfrom(fd, (char *) &buf, sizeof(buf), 0, (struct sockaddr*) &src_addr, &src_addr_len);
            if (n == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    printf("recvfrom timed out, retrying...\n");
                    retries++;
                    continue;
                } else {
                    perror("Error en recvfrom");
                    exit(1);
                }
            }
            if (buf[1] == '3') {
                unsigned short block = (buf[2] << 8) | buf[3];
                if (block == expectedBlock) {
                    fwrite(buf + 4, 1, n - 4, file);
                    unsigned char ack[4];
                    buildAckPackage(ack, block);
                    retries = 0;
                    while (retries < MAX_RETRIES) {
                        int sent = sendto(fd, (char *) &ack, sizeof(ack), 0, (struct sockaddr*) &src_addr, sizeof(src_addr));
                        if (sent == -1) {
                            perror("Error en sendto");
                            retries++;
                        } else {
                            break;
                        }
                    }
                    if (retries == MAX_RETRIES) {
                        printf("Max retries reached, stopping transfer\n");
                        fclose(file);
                        return;
                    }
                    expectedBlock++;
                    if (n < 516) {
                        printf("File transfer completed\n");
                        fclose(file);
                        return;
                    }
                    break;
                }
            }
        }
        if (retries == MAX_RETRIES) {
            printf("Max retries reached, stopping transfer\n");
            break;
        }
    }

    fclose(file);
}


void buildAckPackage(unsigned char *str, unsigned short block) {
    str[0] = '0';
    str[1] = '4';
    str[2] = (unsigned char)((block >> 8) & 0xFF);
    str[3] = (unsigned char)(block & 0xFF);
}
