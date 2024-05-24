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

#define INITIAL_TIMEOUT_SEC 1
#define MAX_RETRIES 10
#define DEFAULT_FILENAME "test.txt"
#define DEFAULT_DEST "test2.txt"
#define DEFAULT_REQ 1

// Tamaño del buffer en donde se reciben los mensajes. Debería ser el tamaño de tftp_packet creo
#define BUFSIZE 100
#define BLOCKSIZE 512

static int fd;
FILE *file;
unsigned short nextBlock = 1;
unsigned short receivedBlock;
int received = 0;
int retries = 0;
struct sockaddr_in src_addr;
socklen_t src_addr_len;
unsigned short ackBlock;
unsigned char dataBuffer[516];
socklen_t src_addr_len;
char filepath[150];

void handler(int signal)
{
    close(fd);
    exit(EXIT_SUCCESS);
}

void waitDataAndSend();
void buildRequestPackage(unsigned char * str, char opcode[2], char filename[100], char mode[100]);
void receiveFile(char * destFilename);
void sendFile();
void sendAckPackage(unsigned short block);
void buildDataPackage(unsigned char * response, unsigned char * fileBuffer, size_t bytesRead, unsigned short blockN);

struct sockaddr_in addr;
char * filename;
char * destFilename;
int reqType;
unsigned char str[202];
unsigned char response[516];
size_t bytesRead;

void buildDataPackage(unsigned char * response, unsigned char * fileBuffer, size_t bytesRead, unsigned short blockN) {
    response[0] = 0;
    response[1] = 3;
    response[2] = (unsigned char)((blockN >> 8) & 0xFF);
    response[3] = (unsigned char)(blockN & 0xFF);
    memcpy(response + 4, fileBuffer, bytesRead);
}

int main(int argc, char* argv[])
{
    // Configura el manejador de señal SIGTERM.
    signal(SIGTERM, handler);

    // Crea el socket.
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Estructura con la dirección donde escuchará el servidor.
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    if (argc == 6) {
        addr.sin_port = htons((uint16_t) atoi(argv[5]));
        inet_aton(argv[4], &(addr.sin_addr));
    } else {
        addr.sin_port = htons(0);
        inet_aton(IP, &(addr.sin_addr));
    }

    // Permite reutilizar la dirección que se asociará al socket.
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
    addr.sin_port = htons(PORT);

    printf("Mandando a %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    if (argc >= 4) {
        filename = argv[1];
        destFilename = argv[2];
        reqType = atoi(argv[3]);
    } else {
        destFilename = DEFAULT_DEST;
        filename = DEFAULT_FILENAME;
        reqType = DEFAULT_REQ;
    }
    // Recibo el archivo
    if (reqType == 1) {
        receiveFile(destFilename);
    } else if (reqType == 2) {
        sendFile();
    } else {
        printf("Not implemented.\n");
    }
    close(fd);
    exit(EXIT_SUCCESS);
}

void buildRequestPackage(unsigned char * str, char opcode[2], char filename[100], char mode[100]) {
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
    i = 0;
    while (mode[i] != '\0') {
        str[i + size] = mode[i];
        i++;
    }
    str[i + size + 2] = '\0';
    return;
}

void sendAckPackage(unsigned short block) {
    unsigned char str[4];
    str[0] = 0;
    str[1] = 4;
    str[2] = (unsigned char)((block >> 8) & 0xFF);
    str[3] = (unsigned char)(block & 0xFF);
    sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));    
}

void receiveFile(char * destFilename) {
    nextBlock = 1;
    // Desp le tendría que poner en los argumentos

    strcpy(filepath, "files/");
    strcat(filepath, destFilename);
    file = fopen(filepath, "wb");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        exit(1);
    }

    // Armo el primer paquete de Read Request
    char opcode[2];
    opcode[0] = 0;
    opcode[1] = 1;
    char mode[100] = "NETASCII";

    buildRequestPackage(str, opcode, filename, mode);

    // Mando el primer paquete de Read Request    
    sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));
    waitDataAndSend();
    return;
}

void waitDataAndSend() {
    for(;;) {
        // Me quedo esperando el paquete de datos
        int received = 0;
        int retries = 0;
        ssize_t receivedBytes;
        while (received == 0 && retries < MAX_RETRIES) {
            
            receivedBytes = recvfrom(fd, (char *) &dataBuffer, 516, 0, (struct sockaddr*) &addr, &src_addr_len);
            
            if (dataBuffer[1] == 5) {
                printf("%s\n", &dataBuffer[2]);
                exit(1);
            }

            if (receivedBytes == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    retries++;
                    if (nextBlock == 1) {
                        sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));
                    }

                    if (retries == MAX_RETRIES) {
                        perror("Max retries reached");
                        exit(1);
                    }
                } else {
                    perror("Error en recvfrom");
                    exit(EXIT_FAILURE);
                }
            } else {
                receivedBlock = (short) ((dataBuffer[2] << 8) | dataBuffer[3]);
                if (receivedBlock != nextBlock) {
                    if (receivedBlock < nextBlock) {
                        sendAckPackage(nextBlock - 1);
                        printf("Recibí otro bloque pero sigo %d %d\n", receivedBlock,nextBlock);
                    } else {
                        // Por ahora salgo, después tendría que manejarlo (aunque creo que nunca debería pasar)
                        printf("Recibí otro bloque %c %d %d", receivedBlock,nextBlock, dataBuffer[1]);
                        exit(1);
                    }
                } else {
                    received = 1;
                    retries = 0;
                }
            }

            
        }

        // Lo recibí bien, escribo en el archivo
        fwrite(dataBuffer + 4, sizeof(unsigned char), receivedBytes - 4, file);
        memset(dataBuffer, 0, sizeof(dataBuffer));

        // Mando el paquete de acknowledge al servidor
        sendAckPackage(nextBlock);

        // Si la data es menos de 512 entonces ya terminé (y ya mandé el acknowledge)
        if (receivedBytes < 516) {
            printf("Dejo de mandarte!\n");
            fclose(file);
            break;
        }
        // Duermo un random de 0 a 3
        /* int randSleep = rand() % (2 + 1 - 0) + 0;
        sleep(randSleep); */
        nextBlock++;
    }
}


void sendFile() {

    strcpy(filepath, "files/");
    strcat(filepath, filename);
    file = fopen(filepath, "r");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        exit(1);
    }

    // Armo el primer paquete de Read Request
    char opcode[2];
    opcode[0] = 0;
    opcode[1] = 2;
    char mode[100] = "NETASCII";

    buildRequestPackage(str, opcode, destFilename, mode);

    unsigned char ackBuf[4];

    received = 0;
    retries = 0;
    ssize_t receivedBytes;
    // Mando el primer paquete de Write Request y espero acknowledge    
    while (received == 0 && retries < MAX_RETRIES) {
        sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));

        receivedBytes = recvfrom(fd, (char *) &ackBuf, 516, 0, (struct sockaddr*) &addr, &src_addr_len);
        
        if (ackBuf[1] == 5) {
            printf("%s\n", &ackBuf[2]);
            exit(1);
        }

        if (receivedBytes == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                retries++;
                if (retries == MAX_RETRIES) {
                    perror("Max retries reached");
                    exit(1);
                }
            } else {
                perror("Error en recvfrom");
                exit(EXIT_FAILURE);
            }
        } else {
            if (ackBuf[1] == 4) {
                received = 1;
                printf("Recibí ack de la req.\n");
            }
        }
    }

    retries = 0;
    int blockN = 1;
    int done = 0;

    unsigned char fileBuffer[512] = {0};
    while (done == 0) {
        memset(fileBuffer, 0, sizeof(fileBuffer));
        memset(response, 0, sizeof(response));
        bytesRead = fread(fileBuffer, 1, sizeof(fileBuffer), file);
        buildDataPackage(response, fileBuffer, bytesRead, blockN);
        printf("%ld \n", bytesRead);
        received = 0;
        retries = 0;
        while (received == 0 && retries < MAX_RETRIES) {
            printf("HOLA\n");
            int n = sendto(fd, (char *) &response, bytesRead + 4, 0, (struct sockaddr*) &addr, sizeof(src_addr));
            if (n == -1) {
                perror("Error al enviar");
                exit(1);
            }
            n = recvfrom(fd, (char *) &ackBuf, 4, 0, (struct sockaddr*) &addr, &src_addr_len);
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
                    printf("RECIBI ACK\n");
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
        /* int randSleep = rand() % (5 + 1 - 0) + 0;
        sleep(randSleep); */
    }
    

}