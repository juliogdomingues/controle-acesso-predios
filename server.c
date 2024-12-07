#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h> // Incluído para errno e EINTR
#include "common.h"

#define MAX_CLIENTS 100

typedef struct {
    int id;
    int sock;
    int loc;
} Client;

static Client clients[MAX_CLIENTS];
static int client_count = 0;

void handle_message(int sock, const char *msg) {
    fprintf(stderr, "< %s\n", msg); // Mensagem recebida

    if (strncmp(msg, "kill", 4) == 0) {
        for (int i = 0; i < client_count; i++) {
            if (clients[i].sock == sock) {
                fprintf(stdout, "Cliente %d enviou 'kill'. SU Successful disconnect\n", clients[i].id);
                clients[i].sock = 0; // Marca o cliente como desconectado
                break;
            }
        }
        close(sock);
        return;
    }
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
        fprintf(stderr, "Limite de clientes atingido.\n");
        close(csock);
        return;
    }

    clients[client_count].id = client_count + 1;
    clients[client_count].sock = csock;
    clients[client_count].loc = (client_count % 2 == 0) ? 1 : 7; // Exemplo
    fprintf(stdout, "Client %d added (Loc %d)\n", clients[client_count].id, clients[client_count].loc);
    client_count++;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <peer_port> <this_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *peer_port = argv[1];
    const char *this_port = argv[2];

    struct sockaddr_storage storage;
    if (server_sockaddr_init("v4", this_port, &storage) != 0) {
        fprintf(stderr, "Erro ao configurar o servidor.\n");
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

    fprintf(stdout, "Servidor rodando na porta %s\n", this_port);

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
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));

                int valread = recv(clients[i].sock, buffer, sizeof(buffer) - 1, 0);
                if (valread == 0) {
                    fprintf(stdout, "Cliente %d desconectado.\n", clients[i].id);
                    close(clients[i].sock);
                    clients[i].sock = 0;
                } else {
                    handle_message(clients[i].sock, buffer);
                }
            }
        }
    }

    close(listen_sock);
    return EXIT_SUCCESS;
}