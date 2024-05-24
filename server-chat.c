#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>

#define PORT 8888
#define IP "127.0.0.1"
#define BUFSIZE 1024
#define BACKLOGSIZE 10

static int server_fd;

typedef struct {
    int socket;
    struct sockaddr_in address;
    char username[50];
} client_t;

client_t *clients[BACKLOGSIZE];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_client(int client_socket);
void broadcast_message(char *message, int sender_socket);
void send_private_message(char *message, char *receiver);
void remove_client(int client_socket);
void handle_file_transfer(int client_socket, char *message);
void handle_sigint(int sig);

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);

    struct sockaddr_in server_addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, IP, &server_addr.sin_addr);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOGSIZE) == -1) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en %s:%d...\n", IP, PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_socket == -1) {
            perror("accept");
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, (void *)handle_client, (void *)(intptr_t)client_socket);
    }

    close(server_fd);
    return 0;
}

void handle_client(int client_socket) {
    char buffer[BUFSIZE];
    int bytes_received;

    while ((bytes_received = recv(client_socket, buffer, BUFSIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("Mensaje recibido: %s\n", buffer);

        if (strncmp(buffer, "LOGIN", 5) == 0) {
            // Handle login
        } else if (strncmp(buffer, "LOGOUT", 6) == 0) {
            // Handle logout
        } else if (strncmp(buffer, "MSG", 3) == 0) {
            // Handle message
            broadcast_message(buffer, client_socket);
        } else if (strncmp(buffer, "FILE", 4) == 0) {
            // Handle file transfer
            handle_file_transfer(client_socket, buffer);
        }
    }

    close(client_socket);
    remove_client(client_socket);
}

void broadcast_message(char *message, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < BACKLOGSIZE; i++) {
        if (clients[i] && clients[i]->socket != sender_socket) {
            send(clients[i]->socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_private_message(char *message, char *receiver) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < BACKLOGSIZE; i++) {
        if (clients[i] && strcmp(clients[i]->username, receiver) == 0) {
            send(clients[i]->socket, message, strlen(message), 0);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int client_socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < BACKLOGSIZE; i++) {
        if (clients[i] && clients[i]->socket == client_socket) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void handle_file_transfer(int client_socket, char *message) {
    // Handle file transfer logic
}

void handle_sigint(int sig) {
    close(server_fd);
    exit(EXIT_SUCCESS);
}
