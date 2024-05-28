#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <sys/stat.h> // Incluir esta cabecera

#define MAX_LINE 100
#define MAX_CLIENTS 10

void* handle_client(void* args);

struct client_info {
    struct sockaddr_in addr;
    socklen_t addr_len;
    int sock;
    char username[20];
};

struct client_info clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[])
{
    pthread_t thread;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    int server_sock, client_sock;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, MAX_CLIENTS) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080\n");

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_len);
        if (client_sock == -1) {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected: %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            close(client_sock);
        } else {
            struct client_info* new_client = &clients[client_count++];
            new_client->sock = client_sock;
            new_client->addr = client_addr;
            new_client->addr_len = client_addr_len;
            pthread_create(&thread, NULL, handle_client, (void*) new_client);
            pthread_detach(thread);
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    close(server_sock);
    return 0;
}

void broadcast_message(char* message, struct client_info* sender) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            send(clients[i].sock, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_file(int client_sock, char* filename) {
    int file_fd = open(filename, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return;
    }

    struct stat file_stat;
    if (fstat(file_fd, &file_stat) < 0) {
        perror("fstat");
        close(file_fd);
        return;
    }

    // Send file size
    off_t file_size = file_stat.st_size;
    send(client_sock, &file_size, sizeof(file_size), 0);

    // Send file
    off_t offset = 0;
    ssize_t sent_bytes = 0;
    while (offset < file_size) {
        sent_bytes = sendfile(client_sock, file_fd, &offset, file_size - offset);
        if (sent_bytes <= 0) {
            perror("sendfile");
            break;
        }
    }

    close(file_fd);
}

void receive_file(int client_sock, char* filename) {
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file_fd == -1) {
        perror("open");
        return;
    }

    // Receive file size
    off_t file_size;
    recv(client_sock, &file_size, sizeof(file_size), 0);

    // Receive file
    char buffer[4096];
    ssize_t received_bytes = 0;
    off_t remaining_bytes = file_size;
    while (remaining_bytes > 0) {
        received_bytes = recv(client_sock, buffer, sizeof(buffer), 0);
        if (received_bytes <= 0) {
            perror("recv");
            break;
        }
        write(file_fd, buffer, received_bytes);
        remaining_bytes -= received_bytes;
    }

    close(file_fd);
}

void* handle_client(void* args) {
    struct client_info* client = (struct client_info*) args;
    char buffer[MAX_LINE];
    int bytes_received;

    // Receive username
    bytes_received = recv(client->sock, client->username, sizeof(client->username), 0);
    if (bytes_received <= 0) {
        close(client->sock);
        return NULL;
    }
    client->username[bytes_received] = '\0';

    printf("New connection from %s:%d as %s\n", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), client->username);

    while ((bytes_received = recv(client->sock, buffer, MAX_LINE, 0)) > 0) {
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "/sendfile ", 10) == 0) {
            char* filename = buffer + 10;
            printf("%s is sending file: %s\n", client->username, filename);
            receive_file(client->sock, filename);
        } else if (strncmp(buffer, "/getfile ", 9) == 0) {
            char* filename = buffer + 9;
            printf("%s is requesting file: %s\n", client->username, filename);
            send_file(client->sock, filename);
        } else {
            char message[MAX_LINE + 50];
            snprintf(message, sizeof(message), "%s: %s", client->username, buffer);
            printf("%s\n", message);
            broadcast_message(message, client);
        }
    }

    // Remove client from list
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] == client) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    printf("%s disconnected\n", client->username);
    close(client->sock);
    return NULL;
}
