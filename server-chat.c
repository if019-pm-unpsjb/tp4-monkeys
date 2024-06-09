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
#define BUFSIZE 2048
#define MAX_USRLEN 20

void* handle_client(void* args);
void send_by_name(char * message, char * username);
void logout_by_name(char *username);

struct client_info
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int sock;
    char username[MAX_USRLEN];
    int ack;
};
void send_error(struct client_info *sender, char *error_message);

struct client_info clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
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
    //inet_aton("192.168.25.111", &(server_addr.sin_addr));

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) == -1)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 1) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080\n");

    memset(clients, 0, sizeof(clients));

    while (1)
    {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock == -1)
        {
            perror("accept");
            continue;
        }

        printf("Me llegó una conexión\n");

        pthread_mutex_lock(&client_mutex);

        if (client_count >= MAX_CLIENTS)
        {
            printf("Max clients reached. Connection rejected: %s:%d\n",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            close(client_sock);
        }
        else
        {
            struct client_info *new_client = &clients[client_count];
            new_client->sock = client_sock;
            new_client->addr = client_addr;
            new_client->addr_len = client_addr_len;
            new_client->ack = 0;
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_client, (void *)new_client) != 0)
            {
                perror("pthread_create");
                close(client_sock); // Cerrar el socket si no se pudo crear el hilo
            }
            else
            {
                pthread_detach(thread);
                client_count++;
            }
        }
        pthread_mutex_unlock(&client_mutex);
    }

    close(server_sock);
    return 0;
}

void listConnectedUsers(void* args) {
    struct client_info* client = (struct client_info*) args;

    // Buffer para almacenar la lista de usuarios
    char clientList[2048];
    int offset = 0;

    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, client->username) != 0) {
            // Añadir el nombre de usuario al buffer
            int len = snprintf(clientList + offset, sizeof(clientList) - offset, "%s\n", clients[i].username);
            offset += len;
        }
    }
    printf("CLIENTS %s\n", clientList);

    send(client->sock, clientList, strlen(clientList), 0);

}

void getDestUser(const char* source, char* destination, size_t maxLen) {
    printf("%s\n", source);
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

void broadcast_message(char* message, struct client_info* sender) {
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            send(clients[i].sock, message, strlen(message), 0);
        }
    }
}

void send_by_name(char * message, char * username) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp((char * ) &clients[i].username,username) == 0) {
            send(clients[i].sock, message, MAX_LINE, 0);
        }
    }
}

void sleep_milliseconds(long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&ts, NULL);
}

void wait_for_ack(struct client_info * sender, struct client_info * client) {
    int retries = 0;
    // Espero acknowledge con timeout manual
    while (client->ack == 0) {
        sleep_milliseconds(100);
        retries++;
        if (retries == 10) {
            send_error(sender, "Error: timedout");
            break;
        }
    }
    client->ack = 0;
}

void send_opcode_by_name(unsigned short opcode, char *username, int wait, struct client_info * sender)
{
    unsigned char str[2];
    str[0] = 0;
    str[1] = opcode;
    printf("%d count\n", client_count);
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp((char *)&clients[i].username, username) == 0)
        {
            send(clients[i].sock, str, sizeof(str), 0);
            printf("MANDE OPCODE %d A %s %d\n", opcode, username, clients[i].sock);
            if (wait == 1) {
                wait_for_ack(sender, &clients[i]);
            }
            printf("ME LLEGO ACK DE %s %d\n", username, clients[i].sock);
        }
    }
}

void broadcast_opcode(unsigned short opcode, struct client_info* sender) {
    unsigned char str[2];
    str[0] = 0;
    str[1] = opcode;
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] != sender) {
            send(clients[i].sock, str, sizeof(str), 0);
            wait_for_ack(sender, &clients[i]);
        }
    }
}

void send_error(struct client_info *sender, char *error_message)
{
    send_opcode_by_name(4, sender->username, 0, sender);
    send(sender->sock, error_message, strlen(error_message), 0);
}

void send_file_to_dest(int file_fd, off_t file_size, struct client_info* destino) {
    for (int i = 0; i < client_count; i++) {
        if (&clients[i] == destino) {
            sendfile(clients[i].sock, file_fd, 0, file_size);
        }
    }
}


void send_file(char * message, struct client_info* sender) {
    char filename[20] = "";
    int i = 0;

    // Extraer el nombre del archivo del mensaje
    while (message[i] != ' ' && message[i] != '\0') {
        i++;
    }
    i++;
    while (message[i] != ' ' && message[i] != '\0') {
        i++;
    }
    i++;
    int j = 0;
    while (message[i] != ' ' && message[i] != '\0') {
        filename[j] = message[i];
        i++;
        j++;
    }
    filename[j] = '\0';
    i++;
    j = 0;

    char username[MAX_USRLEN] = "";

    while (message[i] != ' ' && message[i] != '\0') {
        username[j] = message[i];
        i++;
        j++;
    }

    struct client_info *destino;

    // Obtener cliente destino del mensaje
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp((char *)&clients[i].username, username) == 0)
        {
            destino = &clients[i];
        }
    }

    if (destino == NULL)
    {
        char error_message[MAX_LINE];
        snprintf(error_message, sizeof(error_message), "Error: User %s not connected.\n", username);
        send_error(sender, error_message);
        return;
    }

    // Mando acknowledge al sender para que me mande el archivo
    unsigned char ack[2];
    ack[0] = 0;
    ack[1] = 3;
    send(sender->sock, ack, sizeof(ack), 0);

    off_t file_size;

    // Recibir tamaño del archivo
    int bytes_received = recv(sender->sock, &file_size, sizeof(file_size), 0);

    int client_fd = open("tmp", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (client_fd == -1)
    {
        char error_message[MAX_LINE];
        snprintf(error_message, sizeof(error_message), "Error: File %s not found.\n", filename);
        send_error(sender, error_message);
        perror("open");
        return;
    }

    
    // Recibir archivo del sender
    char file_buffer[BUFSIZE];
    off_t received_size = 0;
    while (received_size < file_size)
    {
        bytes_received = recv(sender->sock, file_buffer, BUFSIZE, 0);
        if (bytes_received <= 0)
        {
            // Manejar error
            break;
        }
        printf("ESCRIBIENDO EN TMP\n");
        write(client_fd, file_buffer, bytes_received);
        received_size += bytes_received;
    }
    close(client_fd);
    client_fd = open("tmp", O_RDONLY);

    printf("%ld - %s\n", received_size, filename);

    // Mandar archivo al destino
    send_opcode_by_name(2, username, 1, sender);

    send(destino->sock, &file_size, sizeof(file_size), 0);
    send(destino->sock, &filename, MAX_LINE, 0);

    // Enviar el archivo a destino
    sendfile(destino->sock, client_fd, 0, file_size);
}

struct client_info *get_destino(char *username)
{
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i].username, username) == 0)
        {
            return &clients[i];
        }
    }
    return NULL;
}

void *handle_client(void *args)
{
    struct client_info *client = (struct client_info *)args;
    char buffer[MAX_LINE];
    int bytes_received;

    // Receive username
    bytes_received = recv(client->sock, client->username, 20, 0);
    if (bytes_received <= 0)
    {
        close(client->sock);
        return NULL;
    }
    client->username[bytes_received] = '\0';

    printf("Nuevo hilo para %s %d\n", client->username, client->sock);

    printf("New connection from %s:%d as %s\n", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), client->username);

    while ((bytes_received = recv(client->sock, buffer, MAX_LINE, 0)) > 0)
    {
        printf("RECIBI %d BYTES\n", bytes_received);
        printf("DE %s\n", client->username);
        buffer[bytes_received] = '\0';

        char message[MAX_LINE + 50] = "";
        char newMessage[MAX_LINE + 50] = "";
        snprintf(message, sizeof(message), "%s: %s", client->username, buffer);

        char dest[MAX_USRLEN] = "";
        getDestUser(message, dest, MAX_USRLEN);

        if (strcmp(dest, "A") == 0)
        {
            broadcast_opcode(1, client);
            removeDestUserFromMsg(message, newMessage);
            broadcast_message(newMessage, client);
        } else if (strcmp(dest, "sendfile") == 0)
        {
            send_file(message, client);
        }
        else if (strcmp(dest, "") != 0)
        {
            struct client_info *tmp = get_destino(dest);
            if (tmp != NULL)
            {
                send_opcode_by_name(1, dest, 1, client);
                removeDestUserFromMsg(message, newMessage);
                send_by_name(newMessage, dest);
            }
            else
            {
                char tmpMsg[MAX_LINE];
                snprintf(tmpMsg, sizeof(tmpMsg), "Error: user %s not connected\n", dest);
                send_error(client, tmpMsg);
            }
        }
        else if (buffer[1] == 3)
        {
            client->ack = 1;
        }
    }

    // Remove client from list
    logout_by_name(client->username);
    printf("%s disconnected\n", client->username);
    close(client->sock);
    return NULL;
}

void logout_by_name(char * username) {
    pthread_mutex_lock(&client_mutex);
    int index = -1;
    // Buscar el usuario
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp((char *)&clients[i].username, username) == 0) {
            index = i;
            char message[MAX_LINE];
            snprintf(message, sizeof(message), "%s %d disconnected", clients[i].username, i);
            broadcast_opcode(1, &clients[i]);
            broadcast_message(message, &clients[i]);
            break;
        }
    }

    if (index != -1) {
        for (int i = index; i < client_count - 1; i++) {
            clients[i] = clients[i + 1];
        }
        memset(&clients[client_count], 0, sizeof(struct client_info));
        client_count--;
    }

    pthread_mutex_unlock(&client_mutex);
}