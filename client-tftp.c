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
    
    //struct tftp_packet buf;
    //struct sockaddr_in src_addr;
    //socklen_t src_addr_len;

    // Armo el primer paquete de Read Request
    char opcode[2] = "01";
    char filename[100] = "test.txt\0";
    char mode[100] = "NETASCII\0";

    char str[202];
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

    // Mando el primer paquete de Read Request    
    sendto(fd, (char *) &str, sizeof(str), 0, (struct sockaddr*) &addr, sizeof(addr));

    FILE *file;
    
    for(;;) {
        file = fopen("test2.txt", "r");
        if (file == NULL) {
            perror("Error al abrir el archivo");
            return EXIT_FAILURE;
        }
        // Me quedo esperando respuesta
        char dataBuffer[516];
        socklen_t src_addr_len;
        ssize_t receivedBytes = recvfrom(fd, (char *) &dataBuffer, 516, 0, (struct sockaddr*) &addr, &src_addr_len);

        if (dataBuffer[1] != '3') {
            perror("Error del servidor");
            exit(1);
        }
        // Lo recibí bien
        fprintf(file, "%s", dataBuffer);
        fclose(file);
        char ack[4];
        ack[0] = '0';
        ack[1] = '4'; 
        ack[2] = '0';
        ack[3] = dataBuffer[3];
        /* printf("ME llegó esto\n");
        printf("%s\n", dataBuffer);
        printf("%s\n", ack); */
        
        sendto(fd, (char *) &ack, sizeof(ack), 0, (struct sockaddr*) &addr, sizeof(addr));            
        
        if (receivedBytes < 516) {
            printf("Dejo de mandarte!\n");
            break;
        }

    }

    close(fd);
    exit(EXIT_SUCCESS);
}
