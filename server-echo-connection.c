#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>

// Dirección por defecto del servidor.
#define PORT 8888
#define IP   "127.0.0.1"

// Tamaño del buffer en donde se reciben los mensajes.
#define BUFSIZE 100
#define BACKLOGSIZE 10

// Descriptor de archivo del socket.
static int fd;

// Cierra el socket al recibir una señal SIGTERM.
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
    fd = socket(AF_INET, SOCK_STREAM, 0);
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

    listen(fd, BACKLOGSIZE);

    struct sockaddr_in src_addr;
    socklen_t src_addr_len;

    accept(fd, (struct sockaddr*) &src_addr, &src_addr_len);

    printf("Escuchando en %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    char buf[BUFSIZE];
    
    for(;;) {
        memset(&src_addr, 0, sizeof(struct sockaddr_in));
        src_addr_len = sizeof(struct sockaddr_in);

        // Recibe un mensaje entrante.
        ssize_t n = recvfrom(fd, buf, BUFSIZE, 0, (struct sockaddr*) &src_addr, &src_addr_len); 
        if(n == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        // Elimina '\n' al final del buffer.
        buf[n-1] = '\0';

        // Imprime dirección del emisor y mensaje recibido.
        printf("[%s:%d] %s\n", inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port), buf);
        buf[n] = '\n';
        sendto(fd, buf, n + 1, 0, (struct sockaddr*) &src_addr, sizeof(src_addr));
    }

    // Cierra el socket.
    close(fd);

    exit(EXIT_SUCCESS);
}

