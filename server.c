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

typedef struct {
    char uid[11];
    int is_special;
    int location;
} User;

User users[MAX_USERS];
int user_count = 0;

// Added for peer connection
int peer_sock = -1;
int my_peer_id = -1;
int peer_id = -1;

void handle_client_message(int client_sock, fd_set *readfds, int client_sockets[]);
void handle_peer_message(int sock);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Peer_Port> <Client_Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int peer_port = atoi(argv[1]);
    int client_port = atoi(argv[2]);

    int server_sock, peer_listen_sock, new_socket, max_sd, activity;
    struct sockaddr_in6 address6, peer_addr6;
    fd_set readfds;
    int client_sockets[MAX_CLIENTS] = {0};

    // Create client socket
    if ((server_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Create peer socket
    if ((peer_listen_sock = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("Error creating peer socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(peer_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure client address
    memset(&address6, 0, sizeof(address6));
    address6.sin6_family = AF_INET6;
    address6.sin6_addr = in6addr_any;
    address6.sin6_port = htons(client_port);

    // Configure peer address
    memset(&peer_addr6, 0, sizeof(peer_addr6));
    peer_addr6.sin6_family = AF_INET6;
    peer_addr6.sin6_addr = in6addr_any;
    peer_addr6.sin6_port = htons(peer_port);

    // Bind client socket
    if (bind(server_sock, (struct sockaddr *)&address6, sizeof(address6)) < 0) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Try to bind peer socket
    if (bind(peer_listen_sock, (struct sockaddr *)&peer_addr6, sizeof(peer_addr6)) < 0) {
        if (errno == EADDRINUSE) {
            // Try to connect to existing peer
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

            send(connect_sock, "REQ_CONNPEER()", 13, 0);
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

    // Listen for client connections
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        max_sd = server_sock;

        // Add peer listener if not connected
        if (peer_sock == -1 && peer_listen_sock > 0) {
            FD_SET(peer_listen_sock, &readfds);
            max_sd = peer_listen_sock > max_sd ? peer_listen_sock : max_sd;
        }

        // Add peer socket if connected
        if (peer_sock != -1) {
            FD_SET(peer_sock, &readfds);
            max_sd = peer_sock > max_sd ? peer_sock : max_sd;
        }

        // Add client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
                max_sd = sd > max_sd ? sd : max_sd;
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("Select failed");
        }

        // Handle kill command
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) != NULL) {
                if (strncmp(input, "kill", 4) == 0) {
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

        // Handle new peer connection
        if (peer_listen_sock > 0 && FD_ISSET(peer_listen_sock, &readfds)) {
            if ((new_socket = accept(peer_listen_sock, NULL, NULL)) < 0) {
                perror("Peer accept failed");
            } else if (peer_sock != -1) {
                send(new_socket, "ERROR(01)", 9, 0);
                close(new_socket);
            } else {
                peer_sock = new_socket;
                printf("Peer 5 connected\n");
            }
        }

        // Handle new client connection
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

        // Handle peer messages
        if (peer_sock != -1 && FD_ISSET(peer_sock, &readfds)) {
            handle_peer_message(peer_sock);
        }

        // Handle client messages
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (FD_ISSET(sd, &readfds)) {
                handle_client_message(sd, &readfds, client_sockets);
            }
        }
    }

    return 0;
}

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
    }
    else if (strncmp(buffer, "RES_CONNPEER", 12) == 0) {
        int pid;
        if (sscanf(buffer, "RES_CONNPEER(%d)", &pid) == 1) {
            peer_id = pid;
            my_peer_id = 5;
            printf("New Peer ID: %d\n", my_peer_id);
            printf("Peer %d connected\n", peer_id);
        }
    }
    else if (strncmp(buffer, "REQ_DISCPEER", 12) == 0) {
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

    // Handle REQ_USRADD command
    if (strncmp(buffer, "REQ_USRADD ", 11) == 0) {
        char *uid = strtok(buffer + 11, " ");
        char *is_special = strtok(NULL, " ");
        if (!uid || strlen(uid) != 10 || !is_special || (*is_special != '0' && *is_special != '1')) {
            send(client_sock, "Invalid REQ_USRADD format.\n", 27, 0);
            printf("< %s\n", buffer);  // Log message received on server
        } else if (user_count >= MAX_USERS) {
            send(client_sock, "User limit exceeded.\n", 21, 0);
            printf("< %s\n", buffer);  // Log message received on server
        } else {
            strcpy(users[user_count].uid, uid);
            users[user_count].is_special = atoi(is_special);
            users[user_count].location = -1; // Default location
            user_count++;
            send(client_sock, "OK 2\n", 6, 0);
            printf("< %s\n", buffer);  // Log message received on server
        }
    }
    // Handle REQ_USRACCESS command (for in/out)
    else if (strncmp(buffer, "REQ_USRACCESS ", 14) == 0) {
        char *uid = strtok(buffer + 14, " ");
        char *direction = strtok(NULL, " ");
        if (!uid || strlen(uid) != 10 || !direction || (strcmp(direction, "in") != 0 && strcmp(direction, "out") != 0)) {
            send(client_sock, "Invalid REQ_USRACCESS format.\n", 30, 0);
            printf("< %s\n", buffer);  // Log message received on server
        } else {
            int found = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].uid, uid) == 0) {
                    found = 1;

                    // Retrieve old location
                    int old_loc = users[i].location;

                    // Update location based on direction
                    if (strcmp(direction, "in") == 0) {
                        users[i].location = i + 1; // Example: Location = i + 1 for simplicity
                    } else if (strcmp(direction, "out") == 0) {
                        users[i].location = -1;
                    }

                    // Send response to client
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "RES_USRACCESS %d", old_loc);
                    send(client_sock, response, strlen(response), 0);
                    printf("< %s\n", buffer);  // Log message received on server
                    break;
                }
            }
            if (!found) {
                send(client_sock, "ERROR(18)", 9, 0);
                printf("< %s\n", buffer);  // Log message received on server
            }
        }
    }
    // Handle REQ_FIND command
    else if (strncmp(buffer, "REQ_FIND ", 9) == 0) {
        char *uid = strtok(buffer + 9, " ");
        if (!uid || strlen(uid) != 10) {
            send(client_sock, "Invalid REQ_FIND format.\n", 25, 0);
            printf("< %s\n", buffer);  // Log message received on server
        } else {
            int found = 0;
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].uid, uid) == 0) {
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "User found: Location=%d, Special=%d\n",
                             users[i].location, users[i].is_special);
                    send(client_sock, response, strlen(response), 0);
                    printf("< %s\n", buffer);  // Log message received on server
                    found = 1;
                    break;
                }
            }
            if (!found) {
                send(client_sock, "User not found.\n", 17, 0);
                printf("< %s\n", buffer);  // Log message received on server
            }
        }
    }
    // Handle REQ_LOCLIST command (for inspect)
    else if (strncmp(buffer, "REQ_LOCLIST ", 12) == 0) {
        char *uid = strtok(buffer + 12, " ");
        char *loc_id_str = strtok(NULL, " ");
        int loc_id = atoi(loc_id_str);

        if (!uid || strlen(uid) != 10 || !loc_id_str) {
            send(client_sock, "Invalid REQ_LOCLIST format.\n", 29, 0);
            printf("< %s\n", buffer);  // Log message received on server
            return;
        }

        // Check if user has permission via REQ_USRAUTH
        int has_permission = 0;
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].uid, uid) == 0) {
                has_permission = users[i].is_special;
                break;
            }
        }

        if (!has_permission) {
            send(client_sock, "ERROR(19)", 9, 0); // Permission denied
            printf("< %s\n", buffer);  // Log message received on server
        } else {
            // Build list of users in location
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
            printf("< %s\n", buffer);  // Log message received on server
        }
    }
    // Handle unknown commands
    else {
        send(client_sock, "Unknown command.\n", 18, 0);
        printf("< %s\n", buffer);  // Log message received on server
    }
}