#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h> // Incluir esta cabecera

#define MAX_LINE 100

void* receive_messages(void* args);

int main(int argc, char *argv[])
{
    pthread_t thread;
    struct sockaddr_in server_addr;
    int sock;
    char username[20];
    char buffer[MAX_LINE];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strcpy(username, argv[2]);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_aton(argv[1], &server_addr.sin_addr);

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

    while (1) {
        printf("You: ");
        fflush(stdout);
        if (fgets(buffer, MAX_LINE, stdin) == NULL) {
            break;
        }
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strncmp(buffer, "/sendfile ", 10) == 0) {
            char* filename = buffer + 10;
            int file_fd = open(filename, O_RDONLY);
            if (file_fd == -1) {
                perror("open");
                continue;
            }

            send(sock, buffer, strlen(buffer), 0);
            struct stat file_stat;
            if (fstat(file_fd, &file_stat) < 0) {
                perror("fstat");
                close(file_fd);
                continue;
            }

            off_t file_size = file_stat.st_size;
            send(sock, &file_size, sizeof(file_size), 0);

            off_t offset = 0;
            ssize_t sent_bytes = 0;
            while (offset < file_size) {
                sent_bytes = sendfile(sock, file_fd, &offset, file_size - offset);
                if (sent_bytes <= 0) {
                    perror("sendfile");
                    break;
                }
            }

            close(file_fd);
        } else if (strncmp(buffer, "/getfile ", 9) == 0) {
            send(sock, buffer, strlen(buffer), 0);

            char* filename = buffer + 9;
            int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (file_fd == -1) {
                perror("open");
                continue;
            }

            off_t file_size;
            recv(sock, &file_size, sizeof(file_size), 0);

            char file_buffer[4096];
            ssize_t received_bytes = 0;
            off_t remaining_bytes = file_size;
            while (remaining_bytes > 0) {
                received_bytes = recv(sock, file_buffer, sizeof(file_buffer), 0);
                if (received_bytes <= 0) {
                    perror("recv");
                    break;
                }
                write(file_fd, file_buffer, received_bytes);
                remaining_bytes -= received_bytes;
            }

            close(file_fd);
        } else {
            if (send(sock, buffer, strlen(buffer), 0) == -1) {
                break;
            }
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
