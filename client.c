// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "common.h"

#define REQ_USRADD 33
#define REQ_USRACCESS 34
#define RES_USRACCESS 35
#define REQ_LOCREG 36
#define REQ_USRLOC 38
#define RES_USRLOC 39
#define ERROR 255
#define OK 0

int connect_to_server(const char *host, const char *port) {
    struct sockaddr_storage storage;
    if (addrparse(host, port, &storage) != 0) {
        fprintf(stderr, "Failed to parse host/port.\n");
        return -1;
    }

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) {
        perror("Error creating socket");
        return -1;
    }

    socklen_t addrlen = (storage.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    if (connect(s, (struct sockaddr *)&storage, addrlen) != 0) {
        perror("Error connecting to server");
        close(s);
        return -1;
    }

    fprintf(stdout, "Connected to %s:%s\n", host, port);
    return s;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <SU_HOST> <SU_PORT> <SL_PORT> <CLIENT_ID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *su_host = argv[1];
    const char *su_port = argv[2];
    const char *sl_port = argv[3];
    const char *client_id = argv[4];
    fprintf(stdout, "Client ID: %s\n", client_id);

    int su_sock = connect_to_server(su_host, su_port);
    if (su_sock < 0) exit(EXIT_FAILURE);

    int sl_sock = connect_to_server(su_host, sl_port);
    if (sl_sock < 0) {
        close(su_sock);
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        if (strncmp(line, "kill", 4) == 0) {
            send(su_sock, "kill", 4, 0);
            send(sl_sock, "kill", 4, 0);
            printf("Client sent 'kill' to servers.\n");
            break;
        } else if (strncmp(line, "add ", 4) == 0) {
            char uid[50];
            int is_special;
            if (sscanf(line, "add %49s %d", uid, &is_special) == 2) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s %d", REQ_USRADD, uid, is_special);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                memset(resp, 0, sizeof(resp));
                if (recv(su_sock, resp, sizeof(resp), 0) > 0) {
                    printf("%s", resp);
                }
            } else {
                printf("Usage: add <UID> <IS_SPECIAL>\n");
            }
        } else if (strncmp(line, "find ", 5) == 0) {
            char uid[50];
            if (sscanf(line, "find %49s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s", REQ_USRLOC, uid);
                send(sl_sock, msg, strlen(msg), 0);
                char resp[256];
                memset(resp, 0, sizeof(resp));
                if (recv(sl_sock, resp, sizeof(resp), 0) > 0) {
                    printf("%s", resp);
                }
            } else {
                printf("Usage: find <UID>\n");
            }
        } else if (strncmp(line, "in ", 3) == 0) {
            char uid[50];
            if (sscanf(line, "in %49s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s in", REQ_USRACCESS, uid);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                memset(resp, 0, sizeof(resp));
                if (recv(su_sock, resp, sizeof(resp), 0) > 0) {
                    printf("SU: %s", resp);
                }
            } else {
                printf("Usage: in <UID>\n");
            }
        } else if (strncmp(line, "out ", 4) == 0) {
            char uid[50];
            if (sscanf(line, "out %49s", uid) == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%d %s out", REQ_USRACCESS, uid);
                send(su_sock, msg, strlen(msg), 0);
                char resp[256];
                memset(resp, 0, sizeof(resp));
                if (recv(su_sock, resp, sizeof(resp), 0) > 0) {
                    printf("SU: %s", resp);
                }
            } else {
                printf("Usage: out <UID>\n");
            }
        } else {
            printf("Unknown command.\n");
        }
    }

    close(su_sock);
    close(sl_sock);
    return EXIT_SUCCESS;
}