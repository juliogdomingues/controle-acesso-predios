#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFFER_SIZE 500

// Função para conectar-se a um servidor, utilizando o IP e a porta especificados
void connect_to_server(const char *ip, int port, int *sock);

int main(int argc, char *argv[]) {
    // Verifica o número de argumentos recebidos
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <IP_SU> <Port_SU> <Port_SL> <LocID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Obtém os parâmetros da linha de comando
    const char *ip_su = argv[1];
    int port_su = atoi(argv[2]);
    int port_sl = atoi(argv[3]);
    int locId = atoi(argv[4]);

    // Verifica se o LocID está dentro do intervalo permitido
    if (locId < 1 || locId > 10) {
        fprintf(stderr, "Invalid LocID. Must be between 1 and 10.\n");
        exit(EXIT_FAILURE);
    }

    int sock_su, sock_sl;
    // Conecta-se ao servidor de usuários e ao servidor de localização
    connect_to_server(ip_su, port_su, &sock_su);
    connect_to_server(ip_su, port_sl, &sock_sl);

    char command[BUFFER_SIZE], response[BUFFER_SIZE];
    while (1) {
        printf("Enter command: ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Remove o caractere de nova linha

        // Comando para encerrar o cliente
        if (strncmp(command, "kill", 4) == 0) {
            printf("Kill command received. Closing all connections and shutting down client.\n");
            close(sock_su);
            close(sock_sl);
            break;
        }

        // Comando para adicionar usuário
        if (strncmp(command, "add ", 4) == 0) {
            // Obtém os parâmetros UID e IS_SPECIAL
            char *uid = strtok(command + 4, " ");
            char *is_special = strtok(NULL, " ");
            if (!uid || strlen(uid) != 10 || !is_special || (*is_special != '0' && *is_special != '1')) {
                printf("Invalid command format. Correct usage: add <UID (10 chars)> <IS_SPECIAL (0 or 1)>\n");
                continue;
            }

            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message), "REQ_USRADD %s %s", uid, is_special);

            // Envia mensagem ao servidor de usuários
            send(sock_su, message, strlen(message), 0);
            printf("> Sent: %s\n", message);

            // Recebe resposta do servidor
            int bytes_received = read(sock_su, response, sizeof(response) - 1);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("< Received: %s\n", response);
            }
            continue;
        }

        // Comando para buscar localização de um usuário
        if (strncmp(command, "find ", 5) == 0) {
            // Obtém o UID
            char *uid = strtok(command + 5, " ");
            if (!uid || strlen(uid) != 10) {
                printf("Invalid command format. Correct usage: find <UID (10 chars)>\n");
                continue;
            }

            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message), "REQ_FIND %s", uid);

            // Envia mensagem ao servidor de localização
            send(sock_sl, message, strlen(message), 0);
            printf("> Sent: %s\n", message);

            // Recebe resposta do servidor
            int bytes_received = read(sock_sl, response, sizeof(response) - 1);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("< Received: %s\n", response);
            }
            continue;
        }

        // Comandos de entrada e saída de usuário
        if (strncmp(command, "in ", 3) == 0 || strncmp(command, "out ", 4) == 0) {
            // Determina se o comando é "in" ou "out"
            char *uid = strtok(command + ((command[1] == 'n') ? 3 : 4), " ");
            if (!uid || strlen(uid) != 10) {
                printf("Invalid command format. Correct usage: in <UID (10 chars)> or out <UID (10 chars)>\n");
                continue;
            }

            char *direction = (strncmp(command, "in ", 3) == 0) ? "in" : "out";

            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message), "REQ_USRACCESS %s %s", uid, direction);

            // Envia mensagem ao servidor de usuários
            send(sock_su, message, strlen(message), 0);
            printf("> Sent: %s\n", message);

            // Recebe resposta do servidor
            int bytes_received = read(sock_su, response, sizeof(response) - 1);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                if (strncmp(response, "ERROR(18)", 9) == 0) {
                    printf("< Received: ERROR(18) - User not found.\n");
                } else {
                    printf("< Received: Ok. Last location: %s\n", response + 15);
                }
            }
            continue;
        }

        // Comando para inspecionar usuários em uma localização
        if (strncmp(command, "inspect ", 8) == 0) {
            // Obtém os parâmetros UID e LocId
            char *uid = strtok(command + 8, " ");
            char *loc_id = strtok(NULL, " ");
            if (!uid || strlen(uid) != 10 || !loc_id) {
                printf("Invalid command format. Correct usage: inspect <UID (10 chars)> <LocId>\n");
                continue;
            }

            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message), "REQ_LOCLIST %s %s", uid, loc_id);

            // Envia mensagem ao servidor de localização
            send(sock_sl, message, strlen(message), 0);
            printf("> Sent: %s\n", message);

            // Recebe resposta do servidor
            int bytes_received = read(sock_sl, response, sizeof(response) - 1);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                if (strncmp(response, "ERROR(19)", 9) == 0) {
                    printf("< Received: ERROR(19) - Permission denied.\n");
                } else if (strncmp(response, "RES_LOCLIST", 11) == 0) {
                    printf("< Received: List of people at the specified location: %s\n", response + 12);
                } else {
                    printf("< Received: Unknown response: %s\n", response);
                }
            }
            continue;
        }

        printf("Unknown command.\n");
    }

    return 0;
}

// Função para conectar-se ao servidor, diferenciando IPv4 e IPv6
void connect_to_server(const char *ip, int port, int *sock) {
    struct sockaddr_in6 serv_addr6;
    struct sockaddr_in serv_addr;
    int is_ipv6 = strchr(ip, ':') != NULL; // Verifica se o endereço é IPv6

    // Criação do socket apropriado
    if ((*sock = socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    if (is_ipv6) {
        // Configuração para IPv6
        memset(&serv_addr6, 0, sizeof(serv_addr6));
        serv_addr6.sin6_family = AF_INET6;
        serv_addr6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, ip, &serv_addr6.sin6_addr) <= 0) {
            perror("Invalid IPv6 address");
            close(*sock);
            exit(EXIT_FAILURE);
        }
        if (connect(*sock, (struct sockaddr *)&serv_addr6, sizeof(serv_addr6)) < 0) {
            perror("Error connecting to IPv6 server");
            close(*sock);
            exit(EXIT_FAILURE);
        }
    } else {
        // Configuração para IPv4
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
            perror("Invalid IPv4 address");
            close(*sock);
            exit(EXIT_FAILURE);
        }
        if (connect(*sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Error connecting to IPv4 server");
            close(*sock);
            exit(EXIT_FAILURE);
        }
    }

    printf("Connected to server on port %d\n", port);
}