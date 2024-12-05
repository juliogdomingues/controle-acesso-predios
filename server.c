#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "common.h"

#define MAX_USERS 30
#define MAX_CLIENTS 10
#define MAX_PEERS 2
#define BUFSZ 500

typedef struct {
    char uid[11];
    int is_special;
} User;

typedef struct {
    char uid[11];
    int location;
} Location;

typedef struct {
    int id;
    int sockfd;
} Peer;

User users[MAX_USERS];
Location locations[MAX_USERS];
Peer peers[MAX_PEERS];
int user_count = 0;
int location_count = 0;
int peer_count = 0;
int client_count = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void handle_su_request(int client_sock, int *client_id) {
    unsigned char buffer[BUFSZ] = {0};
    if (recv(client_sock, buffer, BUFSZ, 0) <= 0) {
        return;
    }

    unsigned char msg_type = buffer[0];
    char *payload = (char *)(buffer + 1);
    unsigned char response[BUFSZ] = {0};
    response[0] = OK;

    switch (msg_type) {
        case REQ_USRADD: { // Adicionar Usuário
            char uid[11];
            int is_special;
            if (sscanf(payload, "%10s %d", uid, &is_special) != 2) {
                snprintf((char *)(response + 1), BUFSZ - 1, "ERROR: Invalid format.");
                response[0] = ERROR;
            } else if (user_count >= MAX_USERS) {
                snprintf((char *)(response + 1), BUFSZ - 1, "ERROR(17): User limit exceeded.");
                response[0] = ERROR;
            } else {
                pthread_mutex_lock(&lock);
                strncpy(users[user_count].uid, uid, 10);
                users[user_count].is_special = is_special;
                user_count++;
                pthread_mutex_unlock(&lock);
                snprintf((char *)(response + 1), BUFSZ - 1, "New user added: %s", uid);
            }
            break;
        }
        default:
            snprintf((char *)(response + 1), BUFSZ - 1, "Unknown request.");
            response[0] = ERROR;
            break;
    }

    send(client_sock, response, strlen((char *)response) + 1, 0);
}

void handle_sl_request(int client_sock, int *client_id) {
    unsigned char buffer[BUFSZ] = {0};
    if (recv(client_sock, buffer, BUFSZ, 0) <= 0) {
        return;
    }

    unsigned char msg_type = buffer[0];
    char *payload = (char *)(buffer + 1);
    unsigned char response[BUFSZ] = {0};
    response[0] = OK;

    switch (msg_type) {
        case REQ_USRLOC: { // Consultar Localização
            char uid[11];
            if (sscanf(payload, "%10s", uid) != 1) {
                snprintf((char *)(response + 1), BUFSZ - 1, "ERROR: Invalid format.");
                response[0] = ERROR;
            } else {
                pthread_mutex_lock(&lock);
                int found = 0;
                for (int i = 0; i < location_count; i++) {
                    if (strncmp(locations[i].uid, uid, 10) == 0) {
                        snprintf((char *)(response + 1), BUFSZ - 1, "Location: %d", locations[i].location);
                        found = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&lock);
                if (!found) {
                    snprintf((char *)(response + 1), BUFSZ - 1, "ERROR(18): User not found.");
                    response[0] = ERROR;
                }
            }
            break;
        }
        default:
            snprintf((char *)(response + 1), BUFSZ - 1, "Unknown request.");
            response[0] = ERROR;
            break;
    }

    send(client_sock, response, strlen((char *)response) + 1, 0);
}
// Função para iniciar o servidor
void *start_server(void *arg) {
    int *params = (int *)arg;
    int port = params[0];
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) error_exit("Socket creation failed");

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) error_exit("Bind failed");
    if (listen(server_sock, MAX_CLIENTS) < 0) error_exit("Listen failed");

    printf("Server listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) continue;

        pthread_mutex_lock(&lock);
        if (client_count >= MAX_CLIENTS) {
            close(client_sock);
            pthread_mutex_unlock(&lock);
            continue;
        }
        client_count++;
        pthread_mutex_unlock(&lock);

        if (port == 50000) {
            handle_su_request(client_sock, &client_count);
        } else if (port == 60000) {
            handle_sl_request(client_sock, &client_count);
        }
        close(client_sock);

        pthread_mutex_lock(&lock);
        client_count--;
        pthread_mutex_unlock(&lock);
    }

    close(server_sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <su_port> <sl_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int su_port = atoi(argv[1]);
    int sl_port = atoi(argv[2]);

    pthread_t su_thread, sl_thread;
    int su_params[] = {su_port};
    int sl_params[] = {sl_port};

    pthread_create(&su_thread, NULL, start_server, su_params);
    pthread_create(&sl_thread, NULL, start_server, sl_params);

    pthread_join(su_thread, NULL);
    pthread_join(sl_thread, NULL);

    return EXIT_SUCCESS;
}