#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "common.h"

#define BUFSZ 500

void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void send_message(int sockfd, unsigned char msg_type, const char *payload) {
    unsigned char buffer[BUFSZ] = {0};
    buffer[0] = msg_type;
    if (payload) {
        strncpy((char *)(buffer + 1), payload, BUFSZ - 1);
    }
    if (send(sockfd, buffer, strlen((char *)buffer), 0) < 0) {
        error_exit("ERROR sending message");
    }
}

void receive_message(int sockfd) {
    unsigned char buffer[BUFSZ] = {0};
    if (recv(sockfd, buffer, BUFSZ, 0) <= 0) {
        error_exit("ERROR receiving message");
    }
    printf("Server response: %s\n", buffer + 1);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <server-SU-IP> <server-SU-port> <server-SL-port> <location-code>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int location = atoi(argv[4]);
    if (location < 1 || location > 10) {
        fprintf(stderr, "Invalid location code. Must be between 1 and 10.\n");
        exit(EXIT_FAILURE);
    }

    int su_sock, sl_sock;
    struct sockaddr_storage su_addr, sl_addr;

    // Conectar ao servidor SU
    if (addrparse(argv[1], argv[2], &su_addr) != 0) {
        error_exit("Error parsing SU address");
    }
    su_sock = socket(su_addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (connect(su_sock, (struct sockaddr *)&su_addr, sizeof(su_addr)) < 0) {
        error_exit("Error connecting to SU");
    }

    // Conectar ao servidor SL
    if (addrparse(argv[1], argv[3], &sl_addr) != 0) {
        error_exit("Error parsing SL address");
    }
    sl_sock = socket(sl_addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (connect(sl_sock, (struct sockaddr *)&sl_addr, sizeof(sl_addr)) < 0) {
        error_exit("Error connecting to SL");
    }

    printf("Connected to SU and SL servers.\n");

    char command[BUFSZ];
    while (1) {
        printf("Enter command (add UID IS_SPECIAL, find UID, or kill): ");
        fgets(command, BUFSZ, stdin);

        if (strncmp(command, "add", 3) == 0) {
            char uid[11];
            int is_special;
            if (sscanf(command + 4, "%10s %d", uid, &is_special) == 2) {
                char payload[BUFSZ];
                snprintf(payload, BUFSZ, "%s %d", uid, is_special);
                send_message(su_sock, REQ_USRADD, payload);
                receive_message(su_sock);
            } else {
                printf("Invalid command format. Usage: add UID IS_SPECIAL\n");
            }
        } else if (strncmp(command, "find", 4) == 0) {
            char uid[11];
            if (sscanf(command + 5, "%10s", uid) == 1) {
                send_message(sl_sock, REQ_USRLOC, uid);
                receive_message(sl_sock);
            } else {
                printf("Invalid command format. Usage: find UID\n");
            }
        } else if (strncmp(command, "kill", 4) == 0) {
            send_message(su_sock, REQ_DISC, NULL);
            send_message(sl_sock, REQ_DISC, NULL);
            break;
        } else {
            printf("Unknown command.\n");
        }
    }

    close(su_sock);
    close(sl_sock);
    printf("Connections closed.\n");
    return EXIT_SUCCESS;
}