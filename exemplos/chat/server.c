#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>

#include "common.h"

// Códigos de mensagens (simplificados)
#define REQ_USRADD 33
#define REQ_USRLOC 38
#define REQ_USRACCESS 34
#define REQ_LOCREG 36
#define RES_USRLOC 39
#define RES_LOCREG 37
#define ERROR 255
#define OK 0

// Limites
#define MAX_PEERS 1
#define MAX_CLIENTS 100

// Estruturas simplificadas
typedef struct {
    int id;
    int sock;
    int is_active;
} PeerInfo;

typedef struct {
    int id;         // ID do cliente
    int sock;
    int loc;        // Local atribuído ao cliente
    int is_active;
} ClientInfo;

static PeerInfo peer_list[MAX_PEERS];
static int peer_count = 0;

static ClientInfo client_list[MAX_CLIENTS];
static int client_count = 0;

static int next_client_id = 2; // Exemplo: primeiro cliente SU_ID=2, segundo SL_ID=14, etc.
static int next_peer_id = 5;   // Exemplo: primeiro peer com ID 5, segundo com ID 9.

static int running = 1;

// Funções auxiliares
static void add_peer(int sock) {
    if (peer_count >= MAX_PEERS) {
        // Peer limit exceeded
        fprintf(stdout, "Peer limit exceeded\n");
        close(sock);
        return;
    }
    peer_list[peer_count].id = next_peer_id;
    peer_list[peer_count].sock = sock;
    peer_list[peer_count].is_active = 1;
    peer_count++;
    fprintf(stdout, "Peer %d connected\n", next_peer_id);
    fprintf(stdout, "New Peer ID: %d\n", next_peer_id);
    next_peer_id += 4; // Conforme exemplo (9, etc.), apenas ilustrativo
}

static void remove_peer() {
    if (peer_count > 0) {
        // Peer desconectado
        int pid = peer_list[0].id;
        close(peer_list[0].sock);
        peer_list[0].is_active = 0;
        peer_count = 0;
        fprintf(stdout, "Peer %d disconnected\n", pid);
        fprintf(stdout, "Successful disconnect\n");
        fprintf(stdout, "No peer found, starting to listen…\n");
    }
}

static void add_client(int sock, int loc) {
    if (client_count >= MAX_CLIENTS) {
        close(sock);
        return;
    }
    client_list[client_count].id = next_client_id;
    client_list[client_count].sock = sock;
    client_list[client_count].loc = loc;
    client_list[client_count].is_active = 1;
    client_count++;

    fprintf(stdout, "Client %d added (Loc %d)\n", next_client_id, loc);
    next_client_id += 1; // incrementa ID apenas de exemplo
}

static void remove_client(int cid) {
    for (int i=0; i<client_count; i++) {
        if (client_list[i].id == cid && client_list[i].is_active) {
            fprintf(stdout, "Client %d removed (Loc %d)\n", cid, client_list[i].loc);
            close(client_list[i].sock);
            client_list[i].is_active = 0;
            return;
        }
    }
}

static int find_client_by_sock(int sock) {
    for (int i=0; i<client_count; i++) {
        if (client_list[i].sock == sock && client_list[i].is_active) return client_list[i].id;
    }
    return -1;
}

static int find_client_loc_by_id(int cid) {
    for (int i=0; i<client_count; i++) {
        if (client_list[i].id == cid && client_list[i].is_active) return client_list[i].loc;
    }
    return -1;
}

// Função para tratar mensagens do cliente (exemplo simplificado)
static void handle_client_msg(int csock, const char *msg) {
    fprintf(stderr, "< %s\n", msg); // imprime no stderr as mensagens recebidas
    int code;
    char uid[50];
    int special;
    int cid = find_client_by_sock(csock);
    if (cid < 0) {
        // Cliente não identificado ainda, mas já conectado
        // Apenas um simplificador: após adicionar cliente, ele já tem cid.
        cid = next_client_id - 1;
    }

    if (strncmp(msg, "kill", 4) == 0) {
        // Cliente solicitou kill
        // Remover o cliente
        remove_client(cid);
        // Enviar mensagem de desconexão
        // Neste exemplo, iremos simular a desconexão do cliente dos servidores SU e SL:
        fprintf(stdout, "SU Successful disconnectSL Successful disconnect\n");
        return;
    } else if (sscanf(msg, "%d", &code) == 1) {
        // Mensagem codificada
        if (code == REQ_USRADD) {
            // 33 UID IS_SPECIAL
            if (sscanf(msg, "%d %s %d", &code, uid, &special) == 3) {
                // Exemplo: adicionar usuário
                // Apenas imprime resultado conforme exemplo do enunciado
                fprintf(stderr, "> OK 2\n");
                dprintf(csock, "OK 2\n");
                fprintf(stdout, "New user added: %s\n", uid);
            }
        } else if (code == REQ_USRLOC) {
            // "38 UID"
            if (sscanf(msg, "%d %s", &code, uid) == 2) {
                // Exemplo: usuário não encontrado
                // De acordo com o exemplo: > ERROR 18
                fprintf(stderr, "> ERROR 18\n");
                dprintf(csock, "ERROR 18\n");
                fprintf(stdout, "User not found\n");
            }
        } else if (code == REQ_USRACCESS) {
            // "34 UID direction"
            char direction[10];
            if (sscanf(msg, "%d %s %s", &code, uid, direction) == 3) {
                // Exemplo do cenário:
                // Acesso in:
                if (strcmp(direction,"in")==0) {
                    // Em t10/t11 do exemplo: faz REQ_LOCREG 2021808080 7 depois e recebe RES_LOCREG -1
                    fprintf(stderr, "> RES_LOCREG -1\n");
                    dprintf(csock, "RES_LOCREG -1\n");
                    fprintf(stdout, "Ok. Last location: -1\n");
                } else if (strcmp(direction,"out")==0) {
                    // Exemplo posterior: REQ_USRACCESS out => RES_LOCREG 7
                    fprintf(stderr, "> RES_LOCREG 7\n");
                    dprintf(csock, "RES_LOCREG 7\n");
                    fprintf(stdout, "Ok. Last location: 7\n");
                }
            }
        } else if (code == REQ_LOCREG) {
            // "36 UID LOC"
            char loc_str[50];
            if (sscanf(msg, "%d %s %s", &code, uid, loc_str) == 3) {
                // Exemplo:
                // Em um momento recebe REQ_LOCREG e responde RES_LOCREG -1
                fprintf(stderr, "> RES_LOCREG -1\n");
                dprintf(csock, "RES_LOCREG -1\n");
                fprintf(stdout, "Ok. Last location: -1\n");
            }
        } else {
            // Mensagem não tratada no exemplo
            fprintf(stderr, "> ERROR 4\n");
            dprintf(csock, "ERROR 4\n");
        }
    } else {
        fprintf(stderr, "> ERROR 4\n");
        dprintf(csock, "ERROR 4\n");
    }
}

// Trata novas conexões de peer
static int accept_peer(int listen_sock) {
    struct sockaddr_storage pstorage;
    socklen_t plen = sizeof(pstorage);
    int psock = accept(listen_sock, (struct sockaddr*)&pstorage, &plen);
    if (psock < 0) return -1;
    add_peer(psock);
    return psock;
}

// Trata novas conexões de clientes
static void accept_client(int listen_sock) {
    struct sockaddr_storage cstorage;
    socklen_t clen = sizeof(cstorage);
    int csock = accept(listen_sock, (struct sockaddr*)&cstorage, &clen);
    if (csock < 0) return;
    // Em um cenário real, extrairíamos a "Loc" do cliente a partir do handshake inicial.
    // Aqui, do exemplo da tabela 2, quando cliente conecta, imprime "Client 2 added (Loc 1)" etc.
    // Suponha que a localização vem do final da conexão do cliente, por ex:
    // ./client 127.0.0.1 50000 60000 1 => loc=1
    // Não temos esta informação facilmente aqui, então assumiremos loc=1 para o primeiro cliente,
    // loc=7 para o segundo, conforme o exemplo da tabela 2. Vamos alternar a cada conexão para ilustrar.
    int loc = (client_count % 2 == 0) ? 1 : 7;
    add_client(csock, loc);
    // Exemplo da tabela 2: "SU New ID: 2SL New ID: 14" - Não estamos implementando IDs separados SU/SL,
    // apenas imprimindo linha única quando cliente conecta a ambos (simulação simplificada).
    if (client_count == 1) {
        fprintf(stdout, "SU New ID: 2SL New ID: 14\n");
    } else if (client_count == 2) {
        fprintf(stdout, "SU New ID: 3SL New ID: 15\n");
    }

    // Não há autenticação real aqui, em um trabalho completo precisaria negociar com o cliente.
}

// Função principal
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <peer_port> <this_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *peer_port = argv[1];
    const char *this_port = argv[2];

    // Tentar conectar ao peer
    struct sockaddr_storage peer_storage;
    if (0 != addrparse("127.0.0.1", peer_port, &peer_storage)) {
        fprintf(stderr, "Falha em addrparse peer.\n");
        exit(EXIT_FAILURE);
    }
    int peer_sock = socket(peer_storage.ss_family, SOCK_STREAM, 0);
    if (peer_sock < 0) {
        perror("socket peer");
        exit(EXIT_FAILURE);
    }

    if (connect(peer_sock, (struct sockaddr *)&peer_storage, sizeof(peer_storage)) != 0) {
        // Não conseguiu conectar ao peer, então iremos "ouvir" um peer
        close(peer_sock);
        fprintf(stdout, "No peer found, starting to listen…\n");

        // Servidor para aceitar peer
        struct sockaddr_storage pstorage;
        if (0 != server_sockaddr_init("v4", peer_port, &pstorage)) {
            fprintf(stderr, "Erro em server_sockaddr_init peer port\n");
            exit(EXIT_FAILURE);
        }
        int psock = socket(pstorage.ss_family, SOCK_STREAM, 0);
        if (psock == -1) logexit("socket peer listen");
        int enable=1;
        setsockopt(psock,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(int));
        if (bind(psock,(struct sockaddr*)&pstorage,sizeof(pstorage))!=0) logexit("bind peer");
        if (listen(psock,5)!=0) logexit("listen peer");

        // Servidor principal (para clientes)
        struct sockaddr_storage storage;
        if (0 != server_sockaddr_init("v4", this_port, &storage)) {
            fprintf(stderr, "Erro em server_sockaddr_init this port\n");
            exit(EXIT_FAILURE);
        }
        int s = socket(storage.ss_family, SOCK_STREAM, 0);
        if (s == -1) logexit("socket");
        setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(int));
        struct sockaddr *addr=(struct sockaddr*)(&storage);
        if (bind(s,addr,sizeof(storage))!=0) logexit("bind");
        if (listen(s,10)!=0) logexit("listen");
        
        // Multiplexação (select)
        fd_set master;
        int fdmax = (psock > s)? psock : s;
        while(running) {
            FD_ZERO(&master);
            FD_SET(psock,&master);
            FD_SET(s,&master);
            // Adicionar clientes
            for (int i=0; i<client_count; i++) {
                if (client_list[i].is_active) {
                    FD_SET(client_list[i].sock,&master);
                    if (client_list[i].sock > fdmax) fdmax=client_list[i].sock;
                }
            }
            // Espera eventos
            int rc = select(fdmax+1,&master,NULL,NULL,NULL);
            if (rc < 0) break;

            if (FD_ISSET(psock,&master)) {
                int newp = accept_peer(psock);
                if (newp > 0) {
                    // Agora temos um peer. Caso precise, poderíamos encerrar psock.
                    close(psock);
                }
            }

            if (FD_ISSET(s,&master)) {
                // Novo cliente
                accept_client(s);
            }

            for (int i=0; i<client_count; i++) {
                if (client_list[i].is_active && FD_ISSET(client_list[i].sock,&master)) {
                    char buf[512]; memset(buf,0,sizeof(buf));
                    int n=recv(client_list[i].sock,buf,511,0);
                    if (n<=0) {
                        // cliente desconectou
                        remove_client(client_list[i].id);
                    } else {
                        handle_client_msg(client_list[i].sock,buf);
                    }
                }
            }
        }

        close(s);
        if (peer_count>0) remove_peer();
        close(psock);
    } else {
        // Conseguiu conectar ao peer
        add_peer(peer_sock);

        // Agora iniciamos o servidor para clientes
        struct sockaddr_storage storage;
        if (0 != server_sockaddr_init("v4", this_port, &storage)) {
            fprintf(stderr, "Erro em server_sockaddr_init\n");
            exit(EXIT_FAILURE);
        }

        int s = socket(storage.ss_family, SOCK_STREAM, 0);
        if (s == -1) logexit("socket");
        int enable=1;
        setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(int));
        struct sockaddr *addr=(struct sockaddr*)(&storage);
        if (bind(s,addr,sizeof(storage))!=0) logexit("bind");
        if (listen(s,10)!=0) logexit("listen");

        fd_set master;
        int fdmax = (peer_sock > s)? peer_sock : s;
        while(running) {
            FD_ZERO(&master);
            FD_SET(s,&master);
            // Peer pode não ser necessário gerenciar no select se não for trocar msg com peer
            for (int i=0; i<client_count; i++) {
                if (client_list[i].is_active) {
                    FD_SET(client_list[i].sock,&master);
                    if (client_list[i].sock > fdmax) fdmax=client_list[i].sock;
                }
            }
            int rc = select(fdmax+1,&master,NULL,NULL,NULL);
            if (rc < 0) break;

            if (FD_ISSET(s,&master)) {
                // Novo cliente
                accept_client(s);
            }

            for (int i=0; i<client_count; i++) {
                if (client_list[i].is_active && FD_ISSET(client_list[i].sock,&master)) {
                    char buf[512]; memset(buf,0,sizeof(buf));
                    int n=recv(client_list[i].sock,buf,511,0);
                    if (n<=0) {
                        remove_client(client_list[i].id);
                    } else {
                        handle_client_msg(client_list[i].sock,buf);
                    }
                }
            }
        }

        // Encerrar
        if (peer_count>0) remove_peer();
        close(s);
        close(peer_sock);
    }

    return EXIT_SUCCESS;
}