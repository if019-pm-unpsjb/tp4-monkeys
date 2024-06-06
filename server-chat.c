#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#define MAX_LINE 100
#define MAX_CLIENTS 10
#define MAX_USRLEN 20

void *handle_client(void *args);
void send_by_name(char *message, char *username);

struct client_info {
    struct sockaddr_in addr;
    socklen_t addr_len;
    int sock;
    char username[MAX_USRLEN];
};

struct client_info clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_sock, client_sock;

void handler(int signal) {
    for (int i = 0; i < client_count; i++) {
        close(clients[i].sock);
    }
    close(server_sock);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    pthread_t thread;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    signal(SIGTERM, handler);
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

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, MAX_CLIENTS) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080\n");

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
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
            struct client_info *new_client = &clients[client_count++];
            new_client->sock = client_sock;
            new_client->addr = client_addr;
            new_client->addr_len = client_addr_len;
            pthread_create(&thread, NULL, handle_client, (void *)new_client);
            pthread_detach(thread);
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    close(server_sock);
    return 0;
}

void listConnectedUsers(void *args) {
    struct client_info *client = (struct client_info *)args;
    char clientList[2048];
    int offset = 0;

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, client->username) != 0) {
            int len = snprintf(clientList + offset, sizeof(clientList) - offset, "%s\n", clients[i].username);
            offset += len;
        }
    }
    printf("CLIENTS %s\n", clientList);
    send(client->sock, clientList, strlen(clientList), 0);
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_opcode(unsigned short opcode, struct client_info *sender) {
    unsigned char str[2];
    str[0] = 0;
    str[1] = opcode;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            send(clients[i].sock, &str, sizeof(str), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void getDestUser(const char *source, char *destination, size_t maxLen) {
    printf("getDestUser called with source: '%s'\n", source);
    const char *start = strchr(source, ':');
    if (start != NULL) {
        start++; // Move past ':'
        while (*start == ' ') start++; // Skip spaces after ':'
        size_t i = 0;
        while (*start != ' ' && *start != '\0' && i < maxLen - 1) {
            destination[i++] = *start++;
        }
        destination[i] = '\0';
    } else {
        strcpy(destination, "A");
    }
    printf("getDestUser result: '%s'\n", destination);
}

void removeDestUserFromMsg(char *str, char *newStr) {
    int j = 0;
    int i = 0;
    while (str[j] != ' ') {
        newStr[i] = str[j];
        j++;
        i++;
    }
    j++;

    while (str[j] != ' ') {
        j++;
    }

    while (str[j] != '\0') {
        newStr[i] = str[j];
        i++;
        j++;
    }
}

void broadcast_message(char *message, struct client_info *sender) {
    char formatted_message[MAX_LINE + MAX_USRLEN + 2]; // "+2" para el ": " y el null terminator
    snprintf(formatted_message, sizeof(formatted_message), "%s: %s", sender->username, message);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            send(clients[i].sock, formatted_message, strlen(formatted_message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}


void send_by_name(char *message, char *username) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            char formatted_message[MAX_LINE + MAX_USRLEN + 2];
            snprintf(formatted_message, sizeof(formatted_message), "%s: %s", username, message);
            send(clients[i].sock, formatted_message, strlen(formatted_message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}


void send_opcode_by_name(unsigned short opcode, char *username) {
    unsigned char str[2];
    str[0] = 0;
    str[1] = opcode;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            send(clients[i].sock, &str, sizeof(str), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_file(char *filename, struct client_info *sender) {
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

    off_t file_size = file_stat.st_size;

    // Send opcode indicating file transfer
    broadcast_opcode(2, sender);

    // Notify the receiver about the file size
    broadcast_message((char *)&file_size, sender);

    // Send the file to all clients except the sender
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            sendfile(clients[i].sock, file_fd, NULL, file_size);
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(file_fd);
}

void *handle_client(void *args) {
    struct client_info *client = (struct client_info *)args;
    char buffer[MAX_LINE];

    int bytes_received = recv(client->sock, client->username, MAX_USRLEN, 0);
    client->username[bytes_received] = '\0';
    printf("User %s connected\n", client->username);

    while ((bytes_received = recv(client->sock, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("Received message: '%s' from user: '%s'\n", buffer, client->username);

        if (strncmp(buffer, ":sendfile", 9) == 0) {
            char filename[MAX_LINE];
            sscanf(buffer + 10, "%s", filename);
            send_file(filename, client);
        } else if (strncmp(buffer, ":list", 5) == 0) {
            listConnectedUsers(client);
        } else if (buffer[0] == '@') {
            char destUser[MAX_USRLEN];
            getDestUser(buffer, destUser, MAX_USRLEN);
            char newStr[MAX_LINE];
            removeDestUserFromMsg(buffer, newStr);
            send_by_name(newStr, destUser);
        } else {
            broadcast_message(buffer, client);
        }
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] == client) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    close(client->sock);
    printf("User %s disconnected\n", client->username);
    return NULL;
}
