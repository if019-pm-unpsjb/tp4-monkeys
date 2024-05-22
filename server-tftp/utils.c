/*contiene funciones auxiliares que ayudan a construir y enviar paquetes de datos y errores*/
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFSIZE 100

extern struct sockaddr_in src_addr;
extern socklen_t src_addr_len;

void buildDataPackage(unsigned char *response, unsigned char *fileBuffer, size_t bytesRead, unsigned short blockN) {
    response[0] = '0';
    response[1] = '3';
    response[2] = (unsigned char)((blockN >> 8) & 0xFF);
    response[3] = (unsigned char)(blockN & 0xFF);
    memcpy(response + 4, fileBuffer, bytesRead);
}

void sendErrorPackage(int fd, unsigned char *message) {
    unsigned char response[100];
    response[0] = '0';
    response[1] = '5';
    memcpy(response + 2, message, strlen((char *)message));
    sendto(fd, (char *)&response, 2 + strlen((char *)message), 0, (struct sockaddr*)&src_addr, sizeof(src_addr));
}
