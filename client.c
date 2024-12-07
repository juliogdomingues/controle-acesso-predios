#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "common.h"

// Códigos de mensagens
#define REQ_USRADD 33
#define REQ_USRACCESS 34
#define RES_USRACCESS 35
#define REQ_LOCREG 36
#define REQ_USRLOC 38
#define RES_USRLOC 39
#define ERROR 255
#define OK 0

int connect_to_server(const char *host, const char *port) {
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(atoi(port));

    if (inet_pton(AF_INET, host, &addr4.sin_addr) <= 0) {
        perror("Erro ao interpretar o endereço IPv4");
        return -1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        perror("Erro ao criar socket");
        return -1;
    }

    if (connect(s, (struct sockaddr *)&addr4, sizeof(addr4)) != 0) {
        perror("Erro ao conectar ao servidor");
        close(s);
        return -1;
    }

    fprintf(stdout, "Conexão bem-sucedida com %s:%s\n", host, port);
    return s;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Uso: %s <SU_HOST> <SU_PORT> <SL_PORT> <CLIENT_ID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *su_host = argv[1];
    const char *su_port = argv[2];
    const char *sl_port = argv[3];

    int su_sock = connect_to_server(su_host, su_port);
    if (su_sock < 0) exit(EXIT_FAILURE);

    int sl_sock = connect_to_server(su_host, sl_port);
    if (sl_sock < 0) exit(EXIT_FAILURE);

    char line[256];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        if (strncmp(line, "kill", 4) == 0) {
            send(su_sock, "kill", 4, 0);
            send(sl_sock, "kill", 4, 0);
            printf("Cliente enviou 'kill' aos servidores.\n");
            break;
        } else if (strncmp(line, "add ", 4) == 0) {
            char uid[50];
            int is_special;
            if (sscanf(line, "add %s %d", uid, &is_special) == 2) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s %d", REQ_USRADD, uid, is_special);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                recv(su_sock, resp, sizeof(resp), 0);
                printf("%s\n", resp);
            }
        } else if (strncmp(line, "find ", 5) == 0) {
            char uid[50];
            if (sscanf(line, "find %s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s", REQ_USRLOC, uid);
                send(sl_sock, msg, strlen(msg), 0);
                char resp[256];
                recv(sl_sock, resp, sizeof(resp), 0);
                printf("%s\n", resp);
            }
        } else if (strncmp(line, "in ", 3) == 0) {
            char uid[50];
            if (sscanf(line, "in %s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s in", REQ_USRACCESS, uid);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                recv(su_sock, resp, sizeof(resp), 0);
                printf("SU: %s\n", resp);
            }
        } else if (strncmp(line, "out ", 4) == 0) {
            char uid[50];
            if (sscanf(line, "out %s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s out", REQ_USRACCESS, uid);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                recv(su_sock, resp, sizeof(resp), 0);
                printf("SU: %s\n", resp);
            }
        } else {
            printf("Comando não reconhecido.\n");
        }
    }

    close(su_sock);
    close(sl_sock);
    return EXIT_SUCCESS;
}