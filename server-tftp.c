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
        addr.sin_port = htons(PORT);
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

    printf("Escuchando en %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    //struct tftp_packet buf;
    char buf[BUFSIZE];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;
    
    for(;;) {
        memset(&src_addr, 0, sizeof(struct sockaddr_in));
        src_addr_len = sizeof(struct sockaddr_in);

        // Recibe un mensaje entrante.
        ssize_t n = recvfrom(fd, (char *) &buf, BUFSIZE, 0, (struct sockaddr*) &src_addr, &src_addr_len);

        if(n == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        // Obtengo los datos del paquete
        // OPCODE
        char opcode[2] = "";
        opcode[0] = (buf[0]);
        opcode[1] = (buf[1]);

        // FILENAME
        char filename[100] = "";
        int i = 2;
        int filenameSize = 0;
        while (buf[i] != '\0') {
            filename[i - 2] = buf[i];
            i++;
            filenameSize++;
        }

        // EL MODE POR AHORA LO IGNORO, NO ME IMPORTA
        /* filenameSize += 2;
        char mode[100];
        i = filenameSize ;
        
        while(buf[i] != '\0') {
            mode[i - filenameSize] = buf[i];
            i++;
        } */

        // El opcode[0] no me importa, siempre es 0. Chequeo el 1:

        if (opcode[1] != '1') {
            perror("Operation not implemented");
            return 1;
        }

        // Si estoy aca entonces me pidieron leer, mando acknowledge
        // Abro el archivo para empezar a mandar los datos.
        FILE *file;
        file = fopen(filename, "r");
        if (file == NULL) {
            perror("Error al abrir el archivo");
            return EXIT_FAILURE;
        }

        size_t bytesRead;

        char response[516];
        int blockN = 0;
        unsigned char fileBuffer[512] = {0};
        while (1) {
            printf("Escuchando en %s:%d ...\n", inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
            // Leo los primeros 512 bytes del archivo
            // Limpio el buffer antes de leer
            memset(fileBuffer, 0, sizeof(fileBuffer));
            memset(response, 0, sizeof(response));
            bytesRead = fread(fileBuffer, (blockN * sizeof(fileBuffer)) + 1, sizeof(fileBuffer), file);
            // Armo los paquetes de data
            //int blockN = 0;
            response[0] = '0';
            response[1] = '3';
            response[2] = '0';
            response[3] = '1';
            memcpy(response + 4, fileBuffer, bytesRead);
            printf("%s\n", response);
            // Mando el paquete de data
            sleep(1);
            int n = sendto(fd, (char *) &response, bytesRead + 4, 0, (struct sockaddr*) &src_addr, sizeof(src_addr));
            if (n == -1) {
                perror("Erorr");
                exit(1);
            }
            char ackBuf[4];
            n = recvfrom(fd, (char *) &ackBuf, 4, 0, (struct sockaddr*) &src_addr, &src_addr_len);
            if (n == -1) {
                perror("Error en rcv");
                exit(1);
            }

            if (bytesRead < 512) {
                printf("Dejo de escucharte!\n");
                break;
            }

        }
    }

    close(fd);

    exit(EXIT_SUCCESS);
}
