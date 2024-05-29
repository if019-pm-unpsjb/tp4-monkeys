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

#define MAX_LINE 100
#define MAX_CLIENTS 10
#define MAX_USRLEN 20

void* handle_client(void* args);
void send_by_name(char * message, char * username);

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

int main(int argc, char *argv[])
{
    pthread_t thread;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    signal(SIGTERM, handler);

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

void getDestUser(const char* source, char* destination, size_t maxLen) {
    size_t i = 0;
    int j = 0;
    while (source[j] != ' ') {
        j++;
    }
    j++;

    if (source[j] == ':') {
        j++;
    } else {
        return;
    }

    while(source[j] != '\0' && i < maxLen - 1) {
        if (source[j] == ' ') {
            break;
        }
        *destination++ = source[j];
        j++;
    }
    *destination = '\0';
}

void removeDestUserFromMsg(char* str, char * newStr) {
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

char* add_opcode(char *message, unsigned char code) {
    // Obtener la longitud del mensaje original
    size_t len = strlen(message);
    
    // Crear un nuevo buffer para el mensaje resultante
    char *new_message = (char*)malloc(len + 2); // +1 para el code y +1 para el terminador nulo
    
    if (new_message == NULL) {
        // Manejo de error si no se puede asignar memoria
        fprintf(stderr, "Error: No se pudo asignar memoria\n");
        exit(1);
    }

    // Agregar el code al principio del nuevo mensaje
    new_message[0] = code;
    
    // Copiar el mensaje original después del code
    strcpy(new_message + 1, message);
    
    return new_message;
}

void broadcast_message(char* message, struct client_info* sender) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            unsigned char code = 1;
            char * newMessage = add_opcode(message, code);
            send(clients[i].sock, newMessage, strlen(newMessage), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_by_name(char * message, char * username) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp((char * ) &clients[i].username,username) == 0) {
            unsigned char code = 1;
            char * newMessage = add_opcode(message, code);
            send(clients[i].sock, newMessage, strlen(newMessage), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_file(char * message) {
    printf("SEND FILE\n");
    char * username = "alecapo";
    char * filename = "test.txt";
    int out_fd = -1;
    printf("ACA?\n");
    int in_fd = open(filename, O_RDONLY);
    if (in_fd < 0) {
        perror("Error al abrir el archivo");
        return;
    }
    printf("%s\n", username);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp((char * ) &clients[i].username, username) == 0) {
            out_fd = clients[i].sock;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            out_fd = clients[i].sock;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (out_fd != -1) {
        printf("SENDING FILE\n");

        // Obtener el tamaño del archivo
        off_t file_size = lseek(in_fd, 0, SEEK_END);
        lseek(in_fd, 0, SEEK_SET);

        // Crear un buffer para el encabezado
        char header[9]; // 1 byte para el opcode y 8 bytes para el tamaño del archivo
        header[0] = 2; // Opcode para transferencia de archivo
        memcpy(header + 1, &file_size, sizeof(file_size));

        // Enviar el encabezado
        if (send(out_fd, header, sizeof(header), 0) < 0) {
            perror("Error al enviar el encabezado del archivo");
            close(in_fd);
            return;
        }

        // Enviar el archivo usando sendfile
        off_t offset = 0;
        ssize_t sent_bytes;
        while (offset < file_size) {
            sent_bytes = sendfile(out_fd, in_fd, &offset, file_size - offset);
            if (sent_bytes <= 0) {
                perror("Error al enviar el archivo");
                break;
            }
        }

        close(in_fd);
    } else {
        printf("Usuario no encontrado\n");
        close(in_fd);
    }
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
        char message[MAX_LINE + 50] = "";
        char newMessage[MAX_LINE + 50] = "";
        snprintf(message, sizeof(message), "%s: %s", client->username, buffer);
        char dest[MAX_USRLEN] = "";
        getDestUser(message, dest, MAX_USRLEN);
        if (strcmp(dest, "A") == 0) {
            removeDestUserFromMsg(message, newMessage);
            broadcast_message(newMessage, client);
        } else if (strcmp(dest, "sendfile") == 0) {
            //removeDestUserFromMsg(message, newMessage);
            send_file(newMessage);
        } else if (strcmp(dest, "") != 0) {
            removeDestUserFromMsg(message, newMessage);
            send_by_name(newMessage, dest);
        } else {
           printf("ERROR NO HAY DESTINATARIO");
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
