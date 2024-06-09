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
#include <sys/stat.h>
#include <sys/sendfile.h>

#define MAX_LINE 1000
#define MAX_USRLEN 20
#define DEFAULT_IP "127.0.0.1"
#define BUFFER_SIZE 1024 
#define COMMANDS_SIZE 3
#define MAX_FNAME 20

const char *COMMANDS[COMMANDS_SIZE] = {":sendfile ", ":listUsers ", ":help "};

void *receive_messages(void *args);

void receive_file(int sock, const char *filename);

int wait_for_ack();
void sleep_milliseconds(long milliseconds);

int sock;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t thread;
int acknowledged;

void handler(int signal)
{
    close(sock);
    exit(EXIT_SUCCESS);
}

int isDestCommand(char * dest) {
    for (int i = 0; i < COMMANDS_SIZE; i++)
    {
        if (strcmp(COMMANDS[i], dest) == 0) {
            return 1;
        }
    }
    return 0;
}

void get_filename(const char *source, char *destination, size_t maxLen)
{
    int j = 0;

    while (source[j] != ' ') {
        j++;
    }
    j++;

    while (source[j] != ' ' && source[j] != '\0') {
        *destination++ = source[j];
        j++;
    }
    *destination = '\0';
}

void getDestUser(const char *source, char *destination, size_t maxLen)
{
    size_t i = 0;
    int j = 0;

    if (source[j] != ':')
    {
        source = "";
        return;
    }

    while (source[j] != '\0' && i < maxLen - 1)
    {
        if (source[j] == ' ')
        {
            break;
        }
        *destination++ = source[j];
        j++;
    }
    *destination++ = ' ';
    *destination = '\0';
}

void addDestUser(const char *name, const char *message, char *destination, size_t maxLen)
{
    int i = 0;
    while (name[i] != '\0')
    {
        *destination++ = name[i];
        i++;
    }
    i++;
    int j = 0;
    while (message[j] != '\0')
    {
        *destination++ = message[j];
        j++;
    }
    *destination = '\0';
}

char username[20];
int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr;
    char buffer[MAX_LINE];
    char message[MAX_LINE];
    signal(SIGTERM, handler);

    if (argc != 3 && argc != 2)
    {
        fprintf(stderr, "Usage: %s <server_ip> <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc == 3)
    {
        strcpy(username, argv[2]);
    }
    else
    {
        strcpy(username, argv[1]);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    if (argc == 3)
    {
        inet_aton(argv[1], &server_addr.sin_addr);
    }
    else
    {
        inet_aton(DEFAULT_IP, &server_addr.sin_addr);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Send username to server
    send(sock, username, 20, 0);

    if (pthread_create(&thread, NULL, receive_messages, (void *)&sock) != 0)
    {
        perror("pthread_create");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Bienvenido al MonkeyChat!\n");
    printf("Para empezar escribí ':help'.\n");

    memset(buffer, 0, sizeof(buffer));
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
        if (strcmp(dest, ":sendfile ") == 0) {

            send(sock, buffer, MAX_LINE, 0);
            int seguir = wait_for_ack();

            if (seguir == 0) {
                continue;
            }

            char fname[MAX_FNAME] = "";
            get_filename(buffer, fname, MAX_FNAME);

            int file_fd = open(fname, O_RDONLY);
            if (file_fd == -1)
            {                    
                perror("open");
                continue;
            }

            struct stat file_stat;
            if (fstat(file_fd, &file_stat) < 0)
            {
                perror("fstat");
                close(file_fd);
                continue;
            }

            off_t file_size = file_stat.st_size;

            // Mando tamaño de archivo
            send(sock, &file_size, sizeof(file_size), 0);

            // Mando archivo
            sendfile(sock, file_fd, 0, file_size);
        } else {
            if (strcmp(dest, "") != 0) {
                if (isDestCommand(dest) == 0) {
                    strcpy(defDest, dest);
                }
                strcpy(message, buffer);
            } else {
                addDestUser(defDest, buffer, message, MAX_LINE);
            }
            if (send(sock, message, MAX_LINE, 0) == -1)
            {
                break;
            }
        }
        
        
        memset(message, 0, sizeof(message));
        memset(buffer, 0, sizeof(buffer));
    }

    close(sock);
    return 0;
}

void *receive_messages(void *args) {
    int socket = *(int *)args;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    while ((bytes_received = recv(socket, buffer, 2, 0)) > 0) {
        if (buffer[1] == 2) {
            unsigned char ack[2];
            ack[0] = 0;
            ack[1] = 3;
            send(socket, ack, sizeof(ack), 0);
            // Recibir tamaño del archivo
            off_t file_size;
            bytes_received = recv(socket, &file_size, sizeof(file_size), 0);
            if (bytes_received <= 0)
            {
                // Manejar error
                break;
            }
            char recv_filename[MAX_LINE];
            char filename[MAX_LINE + strlen(username)];
            
            // Recibir el nombre del archivo
            bytes_received = recv(socket, &recv_filename, MAX_LINE, 0);
            if (bytes_received <= 0)
            {
                // Manejar error
                break;
            }

            snprintf(filename, sizeof(filename), "%s%s", (char *)username, (char *)recv_filename);
            printf("\rRecibiendo archivo %s...\n", filename);
            int output_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (output_fd < 0)
            {
                perror("Error abriendo el archivo de salida");
                exit(EXIT_FAILURE);
            }

            // Recibir y escribir el archivo
            off_t received_size = 0;
            while (received_size < file_size)
            {
                bytes_received = recv(socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received <= 0)
                {
                    // Manejar error
                    break;
                }
                write(output_fd, buffer, bytes_received);
                received_size += bytes_received;
            }
            printf("Archivo escrito con éxito en %s\nYou:", filename);
            fflush(stdout);
            close(output_fd);
        } else if (buffer[1] == 1) {
            pthread_mutex_lock(&client_mutex);
            // MANDAR ACK
            unsigned char ack[2];
            ack[0] = 0;
            ack[1] = 3;
            send(socket, ack, 2, 0);

            // RECIBIR MENSAJE
            bytes_received = recv(socket, buffer, MAX_LINE, 0);
            buffer[bytes_received] = '\0';
            printf("\r%s\nYou: ", buffer);
            fflush(stdout);
            pthread_mutex_unlock(&client_mutex);
        } else if (buffer[1] == 4) {
            pthread_mutex_lock(&client_mutex);
            // RECIBIR MENSAJE DE ERROR1
            bytes_received = recv(socket, buffer, MAX_LINE, 0);
            buffer[bytes_received] = '\0';
            printf("\r%s\nYou: ", buffer);
            fflush(stdout);
            pthread_mutex_unlock(&client_mutex);
        } else if (buffer[1] == 3) {
            // Recibí acknowledge para sendfile
            acknowledged = 1;
        }
    }

    return NULL;
}

void sleep_milliseconds(long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&ts, NULL);
}

int wait_for_ack() {
    // Espero acknowledge con timeout manual
    int retries = 0;
    while (acknowledged == 0) {
        sleep_milliseconds(100);
        retries++;
        if (retries == 5) {
            return 0;
        }
    }
    acknowledged = 0;
    return 1;
}