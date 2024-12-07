#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "common.h"

void logexit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int addrparse(const char *addrstr, const char *portstr, struct sockaddr_storage *storage) {
    if (addrstr == NULL || portstr == NULL) {
        fprintf(stderr, "Endereço ou porta inválidos.\n");
        return -1;
    }

    char *endptr;
    long port = strtol(portstr, &endptr, 10);
    if (*endptr != '\0' || port <= 0 || port > 65535) {
        fprintf(stderr, "Porta inválida: %s\n", portstr);
        return -1;
    }
    uint16_t net_port = htons((uint16_t)port);

    memset(storage, 0, sizeof(*storage));
    struct in_addr inaddr4;
    if (inet_pton(AF_INET, addrstr, &inaddr4) == 1) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)storage;
        addr4->sin_family = AF_INET;
        addr4->sin_port = net_port;
        addr4->sin_addr = inaddr4;
        return 0;
    }

    struct in6_addr inaddr6;
    if (inet_pton(AF_INET6, addrstr, &inaddr6) == 1) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)storage;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = net_port;
        memcpy(&(addr6->sin6_addr), &inaddr6, sizeof(inaddr6));
        return 0;
    }

    fprintf(stderr, "Falha ao interpretar o endereço: %s\n", addrstr);
    return -1;
}

void addrtostr(const struct sockaddr *addr, char *str, size_t strsize) {
    int version = 0;  // Inicialização
    char addrstr[INET6_ADDRSTRLEN + 1] = "";
    uint16_t port = 0;  // Inicialização

    if (addr->sa_family == AF_INET) {
        version = 4;
        struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
        if (!inet_ntop(AF_INET, &(addr4->sin_addr), addrstr, sizeof(addrstr))) {
            logexit("inet_ntop");
        }
        port = ntohs(addr4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        version = 6;
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
        if (!inet_ntop(AF_INET6, &(addr6->sin6_addr), addrstr, sizeof(addrstr))) {
            logexit("inet_ntop");
        }
        port = ntohs(addr6->sin6_port);
    } else {
        logexit("unknown protocol family");
    }

    snprintf(str, strsize, "IPv%d %s %hu", version, addrstr, port);
}

int server_sockaddr_init(const char *proto, const char *portstr, struct sockaddr_storage *storage) {
    char *endptr;
    long port = strtol(portstr, &endptr, 10);
    if (*endptr != '\0' || port <= 0 || port > 65535) {
        fprintf(stderr, "Porta inválida: %s\n", portstr);
        return -1;
    }
    uint16_t net_port = htons((uint16_t)port);

    memset(storage, 0, sizeof(*storage));
    if (strcmp(proto, "v4") == 0) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)storage;
        addr4->sin_family = AF_INET;
        addr4->sin_addr.s_addr = INADDR_ANY;
        addr4->sin_port = net_port;
        return 0;
    } else if (strcmp(proto, "v6") == 0) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)storage;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_addr = in6addr_any;
        addr6->sin6_port = net_port;
        return 0;
    }

    fprintf(stderr, "Protocolo desconhecido: %s\n", proto);
    return -1;
}