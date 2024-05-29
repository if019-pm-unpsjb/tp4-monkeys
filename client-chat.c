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
#include <errno.h>

#define MAX_LINE 100
#define MAX_USRLEN 20
#define DEFAULT_IP "127.0.0.1"
#define BUFFER_SIZE 1024

void* receive_messages(void* args);

void receive_file(int sock, const char *filename);

int sock;
pthread_t thread;

void handler(int signal) {
    close(sock);
    //pthread_exit(&thread);
    exit(EXIT_SUCCESS);
}

void getDestUser(const char* source, char* destination, size_t maxLen) {
    size_t i = 0;
    int j = 0;

    if (source[j] != ':') {
        source = "";
        return;
    }

    while(source[j] != '\0' && i < maxLen - 1) {
        if (source[j] == ' ') {
            break;
        }
        *destination++ = source[j];
        j++;
    }
    *destination++ = ' ';
    *destination = '\0';
}

void addDestUser(const char * name, const char * message, char * destination, size_t maxLen) {
    int i = 0;
    while (name[i] != '\0') {
        *destination++ = name[i];
        i++;
    }
    i++;
    int j = 0;
    while (message[j] != '\0') {
        *destination++ = message[j];
        j++;
    }
    *destination = '\0';
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr;
    char username[20];
    char buffer[MAX_LINE];
    char message[MAX_LINE];
    signal(SIGTERM, handler);

    if (argc != 3 && argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip> <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc == 3) {
        strcpy(username, argv[2]);
    } else {
        strcpy(username, argv[1]);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    if (argc == 3) {
        inet_aton(argv[1], &server_addr.sin_addr);
    } else {
        inet_aton(DEFAULT_IP, &server_addr.sin_addr);
    }

    if (connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Send username to server
    send(sock, username, strlen(username), 0);

    if (pthread_create(&thread, NULL, receive_messages, (void*) &sock) != 0) {
        perror("pthread_create");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char defDest[MAX_USRLEN] = ":A ";
    char dest[MAX_USRLEN] = "";
    while (1) {
        printf("You: ");
        fflush(stdout);
        if (fgets(buffer, MAX_LINE, stdin) == NULL) {
            break;
        }
        buffer[strcspn(buffer, "\n")] = '\0';
        memset(dest, 0, sizeof(dest));
        getDestUser(buffer, dest, MAX_USRLEN);
        // Se especificó el usuario
        if (strcmp(dest, "") != 0) {
            strcpy(defDest, dest);
            strcpy(message, buffer);
        } else {
            addDestUser(defDest, buffer, message, MAX_LINE);
        }
        if (send(sock, message, strlen(message), 0) == -1) {
            break;
        }

    }

    close(sock);
    return 0;
}

void* receive_messages(void* args) {
    int sock = *(int*) args;
    char buffer[MAX_LINE];
    int bytes_received;

    while ((bytes_received = recv(sock, buffer, MAX_LINE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("\r%s\nYou: ", buffer);
        fflush(stdout);
    }

    return NULL;
}
