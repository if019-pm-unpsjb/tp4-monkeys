#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[BUFFER_SIZE];

    // Crear el socket del cliente
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Could not create socket");
        return 1;
    }

    // Preparar la estructura sockaddr_in
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Dirección IP del servidor
    server_addr.sin_port = htons(PORT);

    // Conectar al servidor
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(client_socket);
        return 1;
    }

    printf("Connected to server\n");

    // Loop para enviar mensajes
    while (1) {
        printf("Enter message: ");
        fgets(message, BUFFER_SIZE, stdin);
        message[strcspn(message, "\n")] = 0; // Eliminar el salto de línea al final del mensaje

        // Enviar el mensaje al servidor
        if (send(client_socket, message, strlen(message), 0) == -1) {
            perror("Send failed");
            close(client_socket);
            return 1;
        }

        // Si el mensaje es "exit", salir del loop
        if (strcmp(message, "exit") == 0) {
            break;
        }
    }

    // Cerrar el socket del cliente
    close(client_socket);

    return 0;
}
