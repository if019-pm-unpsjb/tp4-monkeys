#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void *receive_messages(void *arg);

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    pthread_t recv_thread;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    pthread_create(&recv_thread, NULL, receive_messages, &sock);

    while (1) {
        fgets(buffer, BUFFER_SIZE, stdin);
        send(sock, buffer, strlen(buffer), 0);
    }

    close(sock);
    return 0;
}

void *receive_messages(void *arg) {
    int sock = *((int*)arg);
    char buffer[BUFFER_SIZE];
    int len;

    while ((len = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        buffer[len] = '\0';
        printf("%s", buffer);
    }

    return NULL;
}
