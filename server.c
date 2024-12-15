#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_CLIENTS 10
#define MAX_PEERS 1
#define MAX_USERS 30
#define BUFFER_SIZE 500

// Debug macro - imprime na saída de erro
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)

// Estrutura que representa um usuário
typedef struct {
    char uid[11];       // Identificador único do usuário (10 caracteres + '\0')
    int is_special;     // Indica se o usuário tem permissões especiais (0 ou 1)
    int location;       // Localização atual do usuário (-1 se estiver fora de qualquer local)
} User;

// Variáveis globais
User users[MAX_USERS];   
int user_count = 0;      
int peer_sockets[MAX_PEERS]; 
int peer_count = 0;      
int my_peer_id = -1;     
int peer_id = -1;        
int next_client_id = 1;  // Para gerar IDs de cliente incrementalmente

// Protótipos das funções
void handle_peer_message(int peer_sock);
void handle_client_message(int client_sock, int client_sockets[]);
void initialize_test_users(void);
void broadcast_to_peers(const char* message);
void sync_user_data_with_peer(int peer_sock);

// Funções auxiliares
void debug_print_user(const User* user) {
    DEBUG_LOG("User: %s, Special: %d, Location: %d", 
              user->uid, user->is_special, user->location);
}

int find_user(const char* uid) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].uid, uid) == 0) {
            return i;
        }
    }
    return -1;
}


void handle_client_message(int client_sock, int client_sockets[]) {
    char buffer[BUFFER_SIZE];
    int valread = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

    if (valread <= 0) {
        DEBUG_LOG("Client disconnected");
        close(client_sock);
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] == client_sock) {
                client_sockets[i] = 0;
                printf("Client removed\n");
                break;
            }
        }
        return;
    }

    buffer[valread] = '\0';
    DEBUG_LOG("Received from client: %s", buffer);

    // Processamento de comandos
    if (strncmp(buffer, "REQ_USRADD ", 11) == 0) {
        char *uid = strtok(buffer + 11, " ");
        char *is_special = strtok(NULL, " ");
        
        if (!uid || strlen(uid) != 10 || !is_special || (*is_special != '0' && *is_special != '1')) {
            DEBUG_LOG("Invalid REQ_USRADD format");
            send(client_sock, "ERROR(01)", 9, 0);
            return;
        }

        int user_idx = find_user(uid);
        if (user_idx >= 0) {
            // Atualiza usuário existente
            users[user_idx].is_special = atoi(is_special);
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "OK %d", next_client_id++);
            send(client_sock, response, strlen(response), 0);
            printf("User updated: %s\n", uid);
        } else if (user_count < MAX_USERS) {
            // Adiciona novo usuário
            strcpy(users[user_count].uid, uid);
            users[user_count].is_special = atoi(is_special);
            users[user_count].location = -1;
            
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "OK %d", next_client_id++);
            send(client_sock, response, strlen(response), 0);
            
            // Sincroniza com peers
            char sync_msg[BUFFER_SIZE];
            snprintf(sync_msg, sizeof(sync_msg), "SYNC_USER %s %d %d", 
                    uid, users[user_count].is_special, users[user_count].location);
            broadcast_to_peers(sync_msg);
            
            printf("New user added: %s\n", uid);
            user_count++;
        } else {
            send(client_sock, "ERROR(17)", 9, 0);
            DEBUG_LOG("User limit exceeded");
        }
    }
    else if (strncmp(buffer, "REQ_FIND ", 9) == 0) {
        char *uid = strtok(buffer + 9, " ");
        if (!uid || strlen(uid) != 10) {
            DEBUG_LOG("Invalid REQ_FIND format");
            send(client_sock, "ERROR(01)", 9, 0);
            return;
        }

        int user_idx = find_user(uid);
        if (user_idx >= 0) {
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "RES_USRLOC %d", 
                    users[user_idx].location);
            send(client_sock, response, strlen(response), 0);
            DEBUG_LOG("User found at location %d", users[user_idx].location);
        } else {
            send(client_sock, "ERROR(18)", 9, 0);
            DEBUG_LOG("User not found: %s", uid);
        }
    }
    else if (strncmp(buffer, "REQ_USRACCESS ", 14) == 0) {
        char *uid = strtok(buffer + 14, " ");
        char *direction = strtok(NULL, " ");

        if (!uid || strlen(uid) != 10 || !direction || 
            (strcmp(direction, "in") != 0 && strcmp(direction, "out") != 0)) {
            DEBUG_LOG("Invalid REQ_USRACCESS format");
            send(client_sock, "ERROR(01)", 9, 0);
            return;
        }

        int user_idx = find_user(uid);
        if (user_idx >= 0) {
            int old_loc = users[user_idx].location;
            
            // Atualiza localização
            if (strcmp(direction, "in") == 0) {
                users[user_idx].location = user_idx + 1;  // Local baseado no índice
            } else {
                users[user_idx].location = -1;
            }

            // Envia resposta
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "RES_LOCREG %d", old_loc);
            send(client_sock, response, strlen(response), 0);

            // Sincroniza com peers
            char sync_msg[BUFFER_SIZE];
            snprintf(sync_msg, sizeof(sync_msg), "SYNC_USER %s %d %d", 
                    uid, users[user_idx].is_special, users[user_idx].location);
            broadcast_to_peers(sync_msg);
            
            DEBUG_LOG("User %s moved from %d to %d", 
                     uid, old_loc, users[user_idx].location);
        } else {
            send(client_sock, "ERROR(18)", 9, 0);
            DEBUG_LOG("User not found for access: %s", uid);
        }
    }
    else if (strncmp(buffer, "REQ_LOCLIST ", 12) == 0) {
        char *uid = strtok(buffer + 12, " ");
        char *loc_str = strtok(NULL, " ");
        
        if (!uid || strlen(uid) != 10 || !loc_str) {
            DEBUG_LOG("Invalid REQ_LOCLIST format");
            send(client_sock, "ERROR(01)", 9, 0);
            return;
        }

        int loc_id = atoi(loc_str);
        int user_idx = find_user(uid);
        
        if (user_idx < 0) {
            send(client_sock, "ERROR(18)", 9, 0);
            DEBUG_LOG("User not found for loclist: %s", uid);
            return;
        }

        if (!users[user_idx].is_special) {
            send(client_sock, "ERROR(19)", 9, 0);
            DEBUG_LOG("Permission denied for user: %s", uid);
            return;
        }

        char response[BUFFER_SIZE] = "RES_LOCLIST";
        int users_found = 0;

        for (int i = 0; i < user_count; i++) {
            if (users[i].location == loc_id) {
                char user_info[20];
                snprintf(user_info, sizeof(user_info), " %s", users[i].uid);
                strcat(response, user_info);
                users_found = 1;
            }
        }

        if (!users_found) {
            strcat(response, " EMPTY");
        }

        send(client_sock, response, strlen(response), 0);
        DEBUG_LOG("Location list sent for location %d", loc_id);
    }
    else {
        DEBUG_LOG("Unknown command received: %s", buffer);
        send(client_sock, "ERROR(01)", 9, 0);
    }
}

void handle_peer_message(int peer_sock) {
    char buffer[BUFFER_SIZE];
    int valread = recv(peer_sock, buffer, sizeof(buffer) - 1, 0);

    if (valread <= 0) {
        DEBUG_LOG("Peer disconnected");
        close(peer_sock);
        
        for (int i = 0; i < MAX_PEERS; i++) {
            if (peer_sockets[i] == peer_sock) {
                peer_sockets[i] = -1;
                peer_count--;
                printf("Peer disconnected\n");
                printf("No peer found, starting to listen...\n");
                break;
            }
        }
        return;
    }

    buffer[valread] = '\0';
    DEBUG_LOG("Received from peer: %s", buffer);

    if (strncmp(buffer, "REQ_CONNPEER()", 13) == 0) {
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "RES_CONNPEER(%d)", my_peer_id == -1 ? 9 : my_peer_id);
        send(peer_sock, response, strlen(response), 0);
        
        if (my_peer_id == -1) {
            my_peer_id = 9;
            peer_id = 5;
            printf("New Peer ID: %d\n", my_peer_id);
            printf("Peer %d connected\n", peer_id);
        }
    } 
    else if (strncmp(buffer, "RES_CONNPEER(", 12) == 0) {
        int pid;
        if (sscanf(buffer, "RES_CONNPEER(%d)", &pid) == 1) {
            peer_id = pid;
            my_peer_id = 5;
            printf("New Peer ID: %d\n", my_peer_id);
            printf("Peer %d connected\n", peer_id);
        }
    }
    else if (strncmp(buffer, "REQ_DISCPEER(", 12) == 0) {
        int pid;
        if (sscanf(buffer, "REQ_DISCPEER(%d)", &pid) == 1) {
            if (pid == peer_id) {
                send(peer_sock, "OK(01)", 6, 0);
                close(peer_sock);
                
                for (int i = 0; i < MAX_PEERS; i++) {
                    if (peer_sockets[i] == peer_sock) {
                        peer_sockets[i] = -1;
                        peer_count--;
                        printf("Peer %d disconnected\n", pid);
                        printf("No peer found, starting to listen...\n");
                        break;
                    }
                }
            } else {
                send(peer_sock, "ERROR(02)", 9, 0);
                DEBUG_LOG("Invalid peer ID in disconnect request");
            }
        }
    }
    else if (strncmp(buffer, "SYNC_USER ", 10) == 0) {
        char uid[11];
        int is_special, location;
        if (sscanf(buffer, "SYNC_USER %10s %d %d", uid, &is_special, &location) == 3) {
            int user_idx = find_user(uid);
            if (user_idx >= 0) {
                users[user_idx].is_special = is_special;
                users[user_idx].location = location;
                DEBUG_LOG("Updated existing user from peer sync");
            } else if (user_count < MAX_USERS) {
                strcpy(users[user_count].uid, uid);
                users[user_count].is_special = is_special;
                users[user_count].location = location;
                user_count++;
                DEBUG_LOG("Added new user from peer sync");
            }
        }
    }
}

void initialize_test_users(void) {
    strcpy(users[0].uid, "1234567890");
    users[0].is_special = 1;
    users[0].location = 5;
    user_count++;
    DEBUG_LOG("Added test user 1: %s", users[0].uid);

    strcpy(users[1].uid, "0987654321");
    users[1].is_special = 0;
    users[1].location = 2;
    user_count++;
    DEBUG_LOG("Added test user 2: %s", users[1].uid);
}

void broadcast_to_peers(const char* message) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_sockets[i] != -1) {
            send(peer_sockets[i], message, strlen(message), 0);
            DEBUG_LOG("Broadcast to peer: %s", message);
        }
    }
}

void sync_user_data_with_peer(int peer_sock) {
    DEBUG_LOG("Starting user data sync with peer");
    for (int i = 0; i < user_count; i++) {
        char sync_msg[BUFFER_SIZE];
        snprintf(sync_msg, sizeof(sync_msg), "SYNC_USER %s %d %d",
                users[i].uid, users[i].is_special, users[i].location);
        send(peer_sock, sync_msg, strlen(sync_msg), 0);
        DEBUG_LOG("Sent sync for user: %s", users[i].uid);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Peer_Port> <Client_Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    initialize_test_users();

    int peer_port = atoi(argv[1]);
    int client_port = atoi(argv[2]);
    int server_sock, peer_listen_sock, new_socket, max_sd, activity;
    struct sockaddr_in6 peer_addr6, client_addr6;
    fd_set readfds;
    int client_sockets[MAX_CLIENTS] = {0};

    for (int i = 0; i < MAX_PEERS; i++) {
        peer_sockets[i] = -1;
    }

    if ((peer_listen_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating peer socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(peer_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    memset(&peer_addr6, 0, sizeof(peer_addr6));
    peer_addr6.sin6_family = AF_INET6;
    peer_addr6.sin6_addr = in6addr_any;
    peer_addr6.sin6_port = htons(peer_port);

    if (bind(peer_listen_sock, (struct sockaddr *)&peer_addr6, sizeof(peer_addr6)) < 0) {
        if (errno == EADDRINUSE) {
            printf("Peer port in use, connecting as client...\n");

            int connect_sock = socket(AF_INET6, SOCK_STREAM, 0);
            if (connect_sock < 0) {
                perror("Error creating connect_sock");
                exit(EXIT_FAILURE);
            }

            struct sockaddr_in6 peer_connect_addr;
            memset(&peer_connect_addr, 0, sizeof(peer_connect_addr));
            peer_connect_addr.sin6_family = AF_INET6;
            inet_pton(AF_INET6, "::1", &peer_connect_addr.sin6_addr);
            peer_connect_addr.sin6_port = htons(peer_port);

            if (connect(connect_sock, (struct sockaddr *)&peer_connect_addr, sizeof(peer_connect_addr)) < 0) {
                perror("Connect failed");
                close(connect_sock);
                exit(EXIT_FAILURE);
            }

            send(connect_sock, "REQ_CONNPEER()", 13, 0);
            
            char buffer[BUFFER_SIZE];
            int r = recv(connect_sock, buffer, sizeof(buffer) - 1, 0);
            if (r <= 0) {
                fprintf(stderr, "Peer connection refused.\n");
                close(connect_sock);
                exit(EXIT_FAILURE);
            }
            
            buffer[r] = '\0';
            printf("Server response: %s\n", buffer);
            
            for (int i = 0; i < MAX_PEERS; i++) {
                if (peer_sockets[i] == -1) {
                    peer_sockets[i] = connect_sock;
                    peer_count++;
                    break;
                }
            }

            sync_user_data_with_peer(connect_sock);
        } else {
            perror("Bind failed for peer socket");
            exit(EXIT_FAILURE);
        }
    }

    // Configuração do socket para clientes
    if ((server_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating client socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed for client socket");
        exit(EXIT_FAILURE);
    }

    memset(&client_addr6, 0, sizeof(client_addr6));
    client_addr6.sin6_family = AF_INET6;
    client_addr6.sin6_addr = in6addr_any;
    client_addr6.sin6_port = htons(client_port);

    if (bind(server_sock, (struct sockaddr *)&client_addr6, sizeof(client_addr6)) < 0) {
        perror("Bind failed for client socket");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Listen failed for client socket");
        exit(EXIT_FAILURE);
    }

    if (listen(peer_listen_sock, MAX_PEERS) < 0) {
        perror("Listen failed for peer socket");
        exit(EXIT_FAILURE);
    }

    printf("Server is running...\n");
    printf("Listening for peers on port %d\n", peer_port);
    printf("Listening for clients on port %d\n", client_port);

    if (peer_count == 0) {
        printf("No peer found, starting to listen...\n");
    }

    // Loop principal do servidor
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(peer_listen_sock, &readfds);
        FD_SET(server_sock, &readfds);
        max_sd = server_sock > peer_listen_sock ? server_sock : peer_listen_sock;

        for (int i = 0; i < MAX_PEERS; i++) {
            if (peer_sockets[i] != -1) {
                FD_SET(peer_sockets[i], &readfds);
                if (peer_sockets[i] > max_sd) max_sd = peer_sockets[i];
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) {
                FD_SET(client_sockets[i], &readfds);
                if (client_sockets[i] > max_sd) max_sd = client_sockets[i];
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            continue;
        }

        // Trata entrada do teclado (comando kill)
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) != NULL) {
                if (strncmp(input, "kill", 4) == 0) {
                    printf("Shutting down server...\n");
                    
                    char disc_msg[BUFFER_SIZE];
                    snprintf(disc_msg, sizeof(disc_msg), "REQ_DISCPEER(%d)", my_peer_id);
                    broadcast_to_peers(disc_msg);
                    
                    for (int i = 0; i < MAX_PEERS; i++) {
                        if (peer_sockets[i] != -1) {
                            close(peer_sockets[i]);
                        }
                    }
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (client_sockets[i] > 0) {
                            close(client_sockets[i]);
                        }
                    }
                    close(server_sock);
                    close(peer_listen_sock);
                    printf("Successful disconnect\n");
                    exit(0);
                }
            }
        }

        // Aceita nova conexão peer
        if (FD_ISSET(peer_listen_sock, &readfds)) {
            if ((new_socket = accept(peer_listen_sock, NULL, NULL)) < 0) {
                perror("Accept failed for peer");
            } else {
                if (peer_count >= MAX_PEERS) {
                    printf("Peer limit reached. Rejecting new connection.\n");
                    send(new_socket, "ERROR(01)", 9, 0);
                    close(new_socket);
                } else {
                    for (int i = 0; i < MAX_PEERS; i++) {
                        if (peer_sockets[i] == -1) {
                            peer_sockets[i] = new_socket;
                            peer_count++;
                            printf("New peer connected\n");
                            break;
                        }
                    }
                }
            }
        }

        // Aceita nova conexão cliente
        if (FD_ISSET(server_sock, &readfds)) {
            if ((new_socket = accept(server_sock, NULL, NULL)) < 0) {
                perror("Accept failed for client");
            } else {
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        printf("Client %d added\n", next_client_id);
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    printf("Client limit reached. Rejecting new connection.\n");
                    close(new_socket);
                }
            }
        }

        // Processa mensagens dos peers
        for (int i = 0; i < MAX_PEERS; i++) {
            if (peer_sockets[i] != -1 && FD_ISSET(peer_sockets[i], &readfds)) {
                handle_peer_message(peer_sockets[i]);
            }
        }

        // Processa mensagens dos clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0 && FD_ISSET(client_sockets[i], &readfds)) {
                handle_client_message(client_sockets[i], client_sockets);
            }
        }
    }

    // Limpeza final (não deve ser alcançado no uso normal)
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_sockets[i] != -1) {
            close(peer_sockets[i]);
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0) {
            close(client_sockets[i]);
        }
    }
    close(server_sock);
    close(peer_listen_sock);

    return 0;
}