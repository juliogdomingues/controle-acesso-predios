#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_CLIENTS 10
#define MAX_USERS 30
#define BUFFER_SIZE 500

// Estrutura que representa um usuário
typedef struct {
    char uid[11];       // Identificador único do usuário (10 caracteres + '\0')
    int is_special;     // Indica se o usuário tem permissões especiais (0 ou 1)
    int location;       // Localização atual do usuário (-1 se estiver fora de qualquer local)
} User;

User users[MAX_USERS];   // Lista de usuários
int user_count = 0;      // Contador de usuários cadastrados

// Variáveis para conexão com peers
int peer_sock = -1;      // Socket para comunicação com um peer
int my_peer_id = -1;     // Identificador do servidor atual
int peer_id = -1;        // Identificador do peer conectado

// Declaração das funções auxiliares
void handle_client_message(int client_sock, fd_set *readfds, int client_sockets[]);
void handle_peer_message(int sock);

int main(int argc, char *argv[]) {
    // Verifica se o número correto de argumentos foi passado
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Peer_Port> <Client_Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int peer_port = atoi(argv[1]);      // Porta para conexão com peers
    int client_port = atoi(argv[2]);   // Porta para conexões de clientes

    int server_sock, peer_listen_sock, new_socket, max_sd, activity;
    struct sockaddr_in6 address6, peer_addr6;
    fd_set readfds;                    // Conjunto de descritores de arquivos para monitoramento
    int client_sockets[MAX_CLIENTS] = {0}; // Lista de sockets dos clientes conectados

    // Criação do socket para conexões de clientes
    if ((server_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Criação do socket para conexões com peers
    if ((peer_listen_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating peer socket");
        exit(EXIT_FAILURE);
    }

    // Configura o socket para reutilizar o endereço
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(peer_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configurações para conexões de clientes (IPv6)
    memset(&address6, 0, sizeof(address6));
    address6.sin6_family = AF_INET6;
    address6.sin6_addr = in6addr_any;
    address6.sin6_port = htons(client_port);

    // Configurações para conexões com peers (IPv6)
    memset(&peer_addr6, 0, sizeof(peer_addr6));
    peer_addr6.sin6_family = AF_INET6;
    peer_addr6.sin6_addr = in6addr_any;
    peer_addr6.sin6_port = htons(peer_port);

    // Faz o bind do socket para clientes
    if (bind(server_sock, (struct sockaddr *)&address6, sizeof(address6)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Faz o bind do socket para peers
    if (bind(peer_listen_sock, (struct sockaddr *)&peer_addr6, sizeof(peer_addr6)) < 0) {
        if (errno == EADDRINUSE) {
            // Tenta conectar-se a um peer existente
            struct sockaddr_in6 peer_connect_addr;
            int connect_sock = socket(AF_INET6, SOCK_STREAM, 0);

            memset(&peer_connect_addr, 0, sizeof(peer_connect_addr));
            peer_connect_addr.sin6_family = AF_INET6;
            inet_pton(AF_INET6, "::1", &peer_connect_addr.sin6_addr);
            peer_connect_addr.sin6_port = htons(peer_port);

            if (connect(connect_sock, (struct sockaddr *)&peer_connect_addr, sizeof(peer_connect_addr)) < 0) {
                perror("Connect failed");
                exit(EXIT_FAILURE);
            }

            send(connect_sock, "REQ_CONNPEER()", 13, 0); // Envia solicitação de conexão ao peer
            peer_sock = connect_sock;
        } else {
            perror("Bind failed for peer socket");
            exit(EXIT_FAILURE);
        }
    } else {
        if (listen(peer_listen_sock, 1) < 0) {
            perror("Listen failed for peer socket");
            exit(EXIT_FAILURE);
        }
        printf("No peer found, starting to listen...\n");
    }

    // Ouve conexões de clientes
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }

    // Loop principal do servidor
    while (1) {
        FD_ZERO(&readfds);             // Reseta o conjunto de descritores
        FD_SET(server_sock, &readfds); // Adiciona o socket do servidor ao conjunto
        FD_SET(STDIN_FILENO, &readfds); // Adiciona a entrada padrão (teclado) ao conjunto
        max_sd = server_sock;

        // Adiciona o listener de peers ao conjunto, se necessário
        if (peer_sock == -1 && peer_listen_sock > 0) {
            FD_SET(peer_listen_sock, &readfds);
            max_sd = peer_listen_sock > max_sd ? peer_listen_sock : max_sd;
        }

        // Adiciona o socket do peer conectado, se existir
        if (peer_sock != -1) {
            FD_SET(peer_sock, &readfds);
            max_sd = peer_sock > max_sd ? peer_sock : max_sd;
        }

        // Adiciona os sockets dos clientes conectados ao conjunto
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
                max_sd = sd > max_sd ? sd : max_sd;
            }
        }

        // Aguarda por atividade nos sockets
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("Select failed");
        }

        // Verifica se há comandos enviados pelo teclado
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) != NULL) {
                if (strncmp(input, "kill", 4) == 0) {
                    // Finaliza o servidor e desconecta os clientes e peers
                    if (peer_sock != -1) {
                        char msg[BUFFER_SIZE];
                        snprintf(msg, sizeof(msg), "REQ_DISCPEER(%d)", my_peer_id);
                        send(peer_sock, msg, strlen(msg), 0);
                        close(peer_sock);
                    }
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (client_sockets[i] > 0) {
                            close(client_sockets[i]);
                        }
                    }
                    close(server_sock);
                    exit(0);
                }
            }
        }

        // Processa novas conexões de peers
        if (peer_listen_sock > 0 && FD_ISSET(peer_listen_sock, &readfds)) {
            if ((new_socket = accept(peer_listen_sock, NULL, NULL)) < 0) {
                perror("Peer accept failed");
            } else if (peer_sock != -1) {
                send(new_socket, "ERROR(01)", 9, 0); // Não aceita múltiplos peers
                close(new_socket);
            } else {
                peer_sock = new_socket;
                printf("Peer connected\n");
            }
        }

        // Processa novas conexões de clientes
        if (FD_ISSET(server_sock, &readfds)) {
            if ((new_socket = accept(server_sock, NULL, NULL)) < 0) {
                perror("Error accepting connection");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf("Client connected, socket %d\n", new_socket);
                    break;
                }
            }
        }

        // Processa mensagens recebidas de peers
        if (peer_sock != -1 && FD_ISSET(peer_sock, &readfds)) {
            handle_peer_message(peer_sock);
        }

        // Processa mensagens recebidas de clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (FD_ISSET(sd, &readfds)) {
                handle_client_message(sd, &readfds, client_sockets);
            }
        }
    }

    return 0;
}

// Função para tratar mensagens recebidas de um peer
void handle_peer_message(int sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        close(sock);
        peer_sock = -1;
        printf("No peer found, starting to listen...\n");
        return;
    }

    buffer[bytes_read] = '\0';

    if (strncmp(buffer, "REQ_CONNPEER()", 13) == 0) {
        char res[BUFFER_SIZE];
        snprintf(res, sizeof(res), "RES_CONNPEER(%d)", 9);
        send(sock, res, strlen(res), 0);
        my_peer_id = 9;
        peer_id = 5;
    } else if (strncmp(buffer, "RES_CONNPEER", 12) == 0) {
        int pid;
        if (sscanf(buffer, "RES_CONNPEER(%d)", &pid) == 1) {
            peer_id = pid;
            my_peer_id = 5;
            printf("New Peer ID: %d\n", my_peer_id);
            printf("Peer %d connected\n", peer_id);
        }
    } else if (strncmp(buffer, "REQ_DISCPEER", 12) == 0) {
        int pid;
        if (sscanf(buffer, "REQ_DISCPEER(%d)", &pid) == 1) {
            if (pid == peer_id) {
                send(sock, "OK(01)", 6, 0);
                close(sock);
                peer_sock = -1;
                printf("Peer %d disconnected\n", pid);
                printf("No peer found, starting to listen...\n");
            } else {
                send(sock, "ERROR(02)", 9, 0);
            }
        }
    }
}

// Função para tratar mensagens recebidas de um cliente
void handle_client_message(int client_sock, fd_set *readfds, int client_sockets[]) {
    char buffer[BUFFER_SIZE];
    int valread = read(client_sock, buffer, sizeof(buffer));

    if (valread == 0) {
        printf("Client disconnected, socket %d\n", client_sock);
        FD_CLR(client_sock, readfds);
        close(client_sock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] == client_sock) {
                client_sockets[i] = 0;
                break;
            }
        }
        return;
    }

    buffer[valread] = '\0';

    // Processa comandos enviados pelo cliente
    if (strncmp(buffer, "REQ_USRADD ", 11) == 0) {
        char *uid = strtok(buffer + 11, " ");
        char *is_special = strtok(NULL, " ");
        if (!uid || strlen(uid) != 10 || !is_special || (*is_special != '0' && *is_special != '1')) {
            send(client_sock, "Invalid REQ_USRADD format.\n", 27, 0);
        } else if (user_count >= MAX_USERS) {
            send(client_sock, "User limit exceeded.\n", 21, 0);
        } else {
            strcpy(users[user_count].uid, uid);
            users[user_count].is_special = atoi(is_special);
            users[user_count].location = -1; // Localização padrão
            user_count++;
            send(client_sock, "OK 2\n", 6, 0);
        }
    } else if (strncmp(buffer, "REQ_USRACCESS ", 14) == 0) {
        char *uid = strtok(buffer + 14, " ");
        char *direction = strtok(NULL, " ");
        if (!uid || strlen(uid) != 10 || !direction || (strcmp(direction, "in") != 0 && strcmp(direction, "out") != 0)) {
            send(client_sock, "Invalid REQ_USRACCESS format.\n", 30, 0);
        } else {
            int found = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].uid, uid) == 0) {
                    found = 1;

                    // Recupera a localização antiga
                    int old_loc = users[i].location;

                    // Atualiza a localização com base na direção
                    if (strcmp(direction, "in") == 0) {
                        users[i].location = i + 1; // Exemplo: Localização = índice + 1
                    } else if (strcmp(direction, "out") == 0) {
                        users[i].location = -1;
                    }

                    // Envia resposta ao cliente
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "RES_USRACCESS %d", old_loc);
                    send(client_sock, response, strlen(response), 0);
                    break;
                }
            }
            if (!found) {
                send(client_sock, "ERROR(18)", 9, 0);
            }
        }
    } else if (strncmp(buffer, "REQ_FIND ", 9) == 0) {
        char *uid = strtok(buffer + 9, " ");
        if (!uid || strlen(uid) != 10) {
            send(client_sock, "Invalid REQ_FIND format.\n", 25, 0);
        } else {
            int found = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].uid, uid) == 0) {
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "User found: Location=%d, Special=%d\n",
                             users[i].location, users[i].is_special);
                    send(client_sock, response, strlen(response), 0);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                send(client_sock, "User not found.\n", 17, 0);
            }
        }
    } else if (strncmp(buffer, "REQ_LOCLIST ", 12) == 0) {
        char *uid = strtok(buffer + 12, " ");
        char *loc_id_str = strtok(NULL, " ");
        int loc_id = atoi(loc_id_str);

        if (!uid || strlen(uid) != 10 || !loc_id_str) {
            send(client_sock, "Invalid REQ_LOCLIST format.\n", 29, 0);
            return;
        }

        // Verifica se o usuário tem permissão
        int has_permission = 0;
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].uid, uid) == 0) {
                has_permission = users[i].is_special;
                break;
            }
        }

        if (!has_permission) {
            send(client_sock, "ERROR(19)", 9, 0); // Permissão negada
        } else {
            // Constroi a lista de usuários na localização
            char loclist_message[BUFFER_SIZE] = "RES_LOCLIST ";
            int has_users = 0;
            for (int i = 0; i < user_count; i++) {
                if (users[i].location == loc_id) {
                    if (has_users) strcat(loclist_message, ", ");
                    strcat(loclist_message, users[i].uid);
                    has_users = 1;
                }
            }
            if (!has_users) {
                strcat(loclist_message, "No users found");
            }
            send(client_sock, loclist_message, strlen(loclist_message), 0);
        }
    } else {
        send(client_sock, "Unknown command.\n", 18, 0);
    }
}