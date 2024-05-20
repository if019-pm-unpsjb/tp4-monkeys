#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8888
#define IP   "127.0.0.1"

// Tamaño del buffer en donde se reciben los mensajes. Debería ser el tamaño de tftp_packet creo
#define BUFSIZE 100
#define BLOCKSIZE 512

static int fd;


// No lo usamos, podríamos pensar en hacerlo
/* struct tftp_packet {
    short opcode;
    char filename[25];
    char eof1;
    char mode[25];
    char eof2;
}; */

void handler(int signal)
{
    close(fd);
    exit(EXIT_SUCCESS);
}

void buildRequestPackage(unsigned char * str, char opcode[2], char filename[100], char mode[100]);
void receiveFile(char * destFilename);
void buildAckPackage(unsigned char * str, unsigned short block);

struct sockaddr_in addr;

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
    if (argc == 3) {
        addr.sin_port = htons((uint16_t) atoi(argv[2]));
        inet_aton(argv[1], &(addr.sin_addr));
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

    // Asocia el socket con la dirección indicada. Tradicionalmente esta 
    // operación se conoce como "asignar un nombre al socket".
    int b = bind(fd, (struct sockaddr*) &addr, sizeof(addr));
    if (b == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    addr.sin_port = htons(PORT);

    printf("Mandando a %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));


    // Armo el primer paquete de Read Request
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
    // Mando el primer paquete de Read Request    
    sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));
    // Recibo el archivo
    receiveFile(destFilename);
    
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

void buildAckPackage(unsigned char * str, unsigned short block) {
    str[0] = '0';
    str[1] = '4';
    str[2] = (unsigned char)((block >> 8) & 0xFF);
    str[3] = (unsigned char)(block & 0xFF);
}

void receiveFile(char * destFilename) {
    FILE *file;

    unsigned short nextBlock = 0;
    unsigned short receivedBlock;
    // Desp le tendría que poner en los argumentos
    file = fopen(destFilename, "wb");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        exit(1);
    }

    for(;;) {
        // Me quedo esperando el paquete de datos
        unsigned char dataBuffer[516];
        socklen_t src_addr_len;
        ssize_t receivedBytes = recvfrom(fd, (char *) &dataBuffer, 516, 0, (struct sockaddr*) &addr, &src_addr_len);

        if (dataBuffer[1] != '3') {
            perror("Error del servidor");
            exit(1);
        }

        receivedBlock = (short) ((dataBuffer[2] << 8) | dataBuffer[3]);
        if (receivedBlock != nextBlock) {
            // Por ahora salgo, después tendría que manejarlo (aunque creo que nunca debería pasar)
            perror("Recibí otro bloque");
            exit(1);
        }


        // Lo recibí bien, escribo en el archivo
        fwrite(dataBuffer + 4, sizeof(unsigned char), receivedBytes - 4, file);
        memset(dataBuffer, 0, sizeof(dataBuffer));

        // Mando el paquete de acknowledge al servidor
        unsigned char ack[4];
        buildAckPackage(ack, nextBlock);

        sendto(fd, (char *) &ack, sizeof(ack), 0, (struct sockaddr*) &addr, sizeof(addr));            
        
        // Si la data es menos de 512 entonces ya terminé (y ya mandé el acknowledge)
        if (receivedBytes < 516) {
            printf("Dejo de mandarte!\n");
            break;
        }
        nextBlock++;
    }
    return;
}