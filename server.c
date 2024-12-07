// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include "common.h"

#define MAX_CLIENTS 100
#define MAX_PEOPLE 100
#define BUFFER_SIZE 256

// Tipos
#define REQ_USRADD    33
#define REQ_USRACCESS 34
#define REQ_LOCREG    36
#define REQ_USRLOC    38
#define ERROR         255
#define OK            0

#define ERR_USER_NOT_FOUND 18
#define ERR_USER_LIMIT     17

typedef struct {
    int id;
    int sock;
    int loc; // Pode ser usado como identificação da localização do cliente
} Client;

typedef struct {
    int id;
    char name[50];
    int loc; // localização atual (-1 se não definida)
} Person;

static Client clients[MAX_CLIENTS];
static Person people[MAX_PEOPLE];
static int client_count = 0;
static int person_count = 0;
static int peer_connected = 0;

void send_message(int sock, const char *msg) {
    if (sock >= 0 && msg) {
        send(sock, msg, strlen(msg), 0);
    }
}

int find_person_index(const char *uid) {
    for (int i = 0; i < person_count; i++) {
        if (strcmp(people[i].name, uid) == 0) {
            return i;
        }
    }
    return -1;
}

void register_person_server(int sock, const char *uid, int loc) {
    if (person_count >= MAX_PEOPLE) {
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "> ERROR %d\n", ERR_USER_LIMIT);
        send_message(sock, response);
        return;
    }
    people[person_count].id = person_count + 1;
    strncpy(people[person_count].name, uid, sizeof(people[person_count].name) - 1);
    people[person_count].loc = loc;
    person_count++;

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "> OK %d\n", people[person_count - 1].id);
    send_message(sock, response);
}

void locate_person_server(int sock, const char *uid) {
    int idx = find_person_index(uid);
    if (idx < 0) {
        // Não encontrada
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "> ERROR %d\n", ERR_USER_NOT_FOUND);
        send_message(sock, response);
        return;
    }

    if (people[idx].loc == -1) {
        // Usuário sem localização conhecida
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "> ERROR %d\n", ERR_USER_NOT_FOUND);
        send_message(sock, response);
        return;
    }

    // Retorna localização
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "> RES_USRLOC %d\n", people[idx].loc);
    send_message(sock, response);
}

void user_access_server(int sock, const char *uid, const char *action) {
    int idx = find_person_index(uid);
    if (idx < 0) {
        // Usuario não encontrado
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "> ERROR %d\n", ERR_USER_NOT_FOUND);
        send_message(sock, response);
        return;
    }

    if (strcmp(action, "in") == 0) {
        // marca que usuário entrou (loc != -1)
        // Para simplificar, vamos atribuir loc = 1
        people[idx].loc = 1;
        send_message(sock, "> OK\n");
    } else if (strcmp(action, "out") == 0) {
        // marca que usuário saiu
        // loc = -1 significa sem localização
        people[idx].loc = -1;
        send_message(sock, "> OK\n");
    } else {
        // Ação desconhecida
        send_message(sock, "> ERROR 255\n");
    }
}

void handle_message(int sock, const char *msg) {
    if (strncmp(msg, "kill", 4) == 0) {
        // kill recebido: desconecta cliente
        send_message(sock, "SU Successful disconnect\n");
        close(sock);
        for (int i = 0; i < client_count; i++) {
            if (clients[i].sock == sock) {
                clients[i].sock = 0;
                break;
            }
        }
        return;
    }

    int code;
    char uid[50];
    int spec;

    // Tenta interpretar REQ_USRADD (33 UID IS_SPECIAL)
    if (sscanf(msg, "%d %49s %d", &code, uid, &spec) == 3 && code == REQ_USRADD) {
        register_person_server(sock, uid, spec);
        return;
    }

    // Tenta interpretar REQ_USRLOC (38 UID)
    if (sscanf(msg, "%d %49s", &code, uid) == 2 && code == REQ_USRLOC) {
        locate_person_server(sock, uid);
        return;
    }

    // Tenta interpretar REQ_USRACCESS (34 UID in/out)
    {
        char action[10];
        if (sscanf(msg, "%d %49s %9s", &code, uid, action) == 3 && code == REQ_USRACCESS) {
            user_access_server(sock, uid, action);
            return;
        }
    }

    // Caso não reconheça o comando
    send_message(sock, "> ERROR 255\n");
}

void accept_client(int listen_sock) {
    struct sockaddr_storage cstorage;
    socklen_t clen = sizeof(cstorage);
    int csock = accept(listen_sock, (struct sockaddr *)&cstorage, &clen);
    if (csock < 0) {
        perror("accept");
        return;
    }

    if (client_count >= MAX_CLIENTS) {
        fprintf(stdout, "Peer limit exceeded\n");
        close(csock);
        return;
    }

    clients[client_count].id = client_count + 1;
    clients[client_count].sock = csock;
    clients[client_count].loc = -1;
    fprintf(stdout, "New user added: %d\n", clients[client_count].id);
    client_count++;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <peer_port> <this_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *peer_port = argv[1];
    const char *this_port = argv[2];

    fprintf(stdout, "Peer port: %s, This port: %s\n", peer_port, this_port);

    // Tenta conectar ao peer (IPv4)
    struct sockaddr_storage peer_storage;
    if (server_sockaddr_init("v4", peer_port, &peer_storage) == 0) {
        int psock = socket(peer_storage.ss_family, SOCK_STREAM, 0);
        if (psock >= 0) {
            socklen_t addrlen = (peer_storage.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            if (connect(psock, (struct sockaddr *)&peer_storage, addrlen) != 0) {
                fprintf(stdout, "No peer found, starting to listen...\n");
                close(psock);
            } else {
                fprintf(stdout, "Peer 5 connected\n");
                peer_connected = 1;
                close(psock);
            }
        } else {
            fprintf(stdout, "No peer found, starting to listen...\n");
        }
    } else {
        fprintf(stdout, "No peer found, starting to listen...\n");
    }

    // Cadastro inicial para teste
    // Como sugerido, já deixamos algo cadastrado para teste:
    // Caso queira testar localização com sucesso, descomente a linha abaixo:
    // register_person_server(-1, "2021808080", 7);
    // Por padrão, deixaremos Alice com loc=10
    person_count = 0; // limpamos para evitar duplo cadastro
    people[person_count].id = 1;
    strncpy(people[person_count].name, "Alice", sizeof(people[person_count].name)-1);
    people[person_count].loc = 10;
    person_count++;

    struct sockaddr_storage storage;
    if (server_sockaddr_init("v4", this_port, &storage) != 0) {
        fprintf(stderr, "Error setting up server.\n");
        exit(EXIT_FAILURE);
    }

    int listen_sock = socket(storage.ss_family, SOCK_STREAM, 0);
    if (listen_sock < 0) logexit("socket");

    int enable = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        logexit("setsockopt");
    }

    if (bind(listen_sock, (struct sockaddr *)&storage, sizeof(struct sockaddr_in)) != 0) {
        logexit("bind");
    }

    if (listen(listen_sock, 10) != 0) {
        logexit("listen");
    }

    fprintf(stdout, "Starting to listen on port %s\n", this_port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(listen_sock, &readfds);
        int max_fd = listen_sock;

        for (int i = 0; i < client_count; i++) {
            if (clients[i].sock > 0) {
                FD_SET(clients[i].sock, &readfds);
                if (clients[i].sock > max_fd) max_fd = clients[i].sock;
            }
        }

        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select");
        }

        if (FD_ISSET(listen_sock, &readfds)) {
            accept_client(listen_sock);
        }

        for (int i = 0; i < client_count; i++) {
            if (clients[i].sock > 0 && FD_ISSET(clients[i].sock, &readfds)) {
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));

                int valread = recv(clients[i].sock, buffer, sizeof(buffer) - 1, 0);
                if (valread == 0) {
                    fprintf(stdout, "Client %d removed (Loc %d)\n", clients[i].id, clients[i].loc);
                    close(clients[i].sock);
                    clients[i].sock = 0;
                } else {
                    fprintf(stderr, "< %s\n", buffer);
                    handle_message(clients[i].sock, buffer);
                }
            }
        }
    }

    close(listen_sock);
    return EXIT_SUCCESS;
}