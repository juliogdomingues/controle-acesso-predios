#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_CLIENTS   10
#define MAX_PEERS     1
#define MAX_USERS     30
#define BUFFER_SIZE   500

static int is_su = 0;  // 1 => Servidor de Usuários (SU), 0 => Servidor de Localização (SL)

// ----------------- Estruturas de dados
// SU: [uid, is_special]
typedef struct {
    char uid[11];
    int  is_special; // 0 ou 1
} SU_User;
static SU_User su_users[MAX_USERS];
static int su_count = 0;

// SL: [uid, location]
typedef struct {
    char uid[11];
    int  location;   // -1 ou [1..10]
} SL_Record;
static SL_Record sl_records[MAX_USERS];
static int sl_count = 0;

// Peer
static int peer_sockets[MAX_PEERS];
static int peer_count = 0;

// Clientes
static int client_sockets[MAX_CLIENTS];
static int client_ids[MAX_CLIENTS];
static int client_locs[MAX_CLIENTS];
static int next_client_id = 2;

// Adiciona variáveis para faixas de ID de clientes e peers
static int su_next_client_id=2;
static int sl_next_client_id=14;
static int su_next_peer_id=9;
static int sl_next_peer_id=5;
static int next_peer_id=0;
static int peer_id = 0;

static int  g_pending_inspect_in_use = 0;
static char g_pending_inspect_uid[11];
static int  g_pending_inspect_loc = -1;
static int  g_pending_inspect_sock = 0;

// ----------------- Declarações de funções
int  find_su_user(const char* uid);
int  find_sl_record(const char* uid);

void handle_client_message(int client_sock);
void process_client_line(int client_sock, const char* line);

void handle_peer_message(int peer_sock);
void process_peer_line(int peer_sock, const char* line);

int  get_client_index_by_socket(int sock);
void close_and_remove_client(int sock);

void send_req_discpeer_and_exit();  // kill

// ----------------------------------------------------
// Funções auxiliares
int find_su_user(const char* uid) {
    for (int i=0; i<su_count; i++) {
        if (strcmp(su_users[i].uid, uid)==0) {
            return i;
        }
    }
    return -1;
}
int find_sl_record(const char* uid) {
    for (int i=0; i<sl_count; i++) {
        if (strcmp(sl_records[i].uid, uid)==0) {
            return i;
        }
    }
    return -1;
}
int get_client_index_by_socket(int sock) {
    for (int i=0; i<MAX_CLIENTS; i++){
        if (client_sockets[i] == sock) {
            return i;
        }
    }
    return -1;
}
void close_and_remove_client(int sock) {
    int idx = get_client_index_by_socket(sock);
    if (idx>=0){
        int c_id = client_ids[idx];
        int loc  = client_locs[idx];
        printf("Client %d removed (Loc %d)\n", c_id, loc);

        close(client_sockets[idx]);
        client_sockets[idx]=0;
        client_ids[idx]=0;
        client_locs[idx]=0;

        if (is_su) {
            printf("SU Successful disconnect\n");
        } else {
            printf("SL Successful disconnect\n");
        }
    }
}

// kill
void send_req_discpeer_and_exit(){
    if (peer_count>0 && peer_sockets[0]!=-1){
        char msg[BUFFER_SIZE];
        snprintf(msg,sizeof(msg),"REQ_DISCPEER(%d)\n", peer_id);
        send(peer_sockets[0], msg, strlen(msg),0);
    }
    for (int i=0; i<MAX_CLIENTS; i++){
        if (client_sockets[i]>0){
            close(client_sockets[i]);
            client_sockets[i]=0;
        }
    }
    for (int i=0;i<MAX_PEERS;i++){
        if (peer_sockets[i]!=-1){
            close(peer_sockets[i]);
            peer_sockets[i]=-1;
        }
    }
    printf("Successful disconnect\n");
    printf("Peer %d disconnected\n", peer_id);
    exit(0);
}

// ----------------------------------------------------
// Mensagens de cliente
void handle_client_message(int client_sock){
    char buffer[BUFFER_SIZE];
    int valread = recv(client_sock, buffer, sizeof(buffer)-1, 0);
    if (valread<=0) {
        // desconectar
        close_and_remove_client(client_sock);
        return;
    }
    buffer[valread] = '\0';

    char* line = strtok(buffer, "\n");
    while (line) {
        process_client_line(client_sock, line);
        line = strtok(NULL, "\n");
    }
}

typedef struct {
    char uid[11];
    int  client_sock;
} SU_UsrAccessReq;

static SU_UsrAccessReq su_uar[MAX_USERS];
static int su_uar_count = 0;

// Salva (uid -> socket)
static void su_uar_add(const char* uid, int c_sock){
    for (int i=0; i<su_uar_count; i++){
        if(strcmp(su_uar[i].uid, uid)==0){
            su_uar[i].client_sock = c_sock;
            return;
        }
    }
    if(su_uar_count<MAX_USERS){
        strcpy(su_uar[su_uar_count].uid, uid);
        su_uar[su_uar_count].client_sock = c_sock;
        su_uar_count++;
    }
}

// Retorna e remove
static int su_uar_remove(const char* uid){
    for(int i=0;i<su_uar_count;i++){
        if(strcmp(su_uar[i].uid,uid)==0){
            int sock = su_uar[i].client_sock;
            su_uar_count--;
            su_uar[i] = su_uar[su_uar_count];
            return sock;
        }
    }
    return -1;
}

void process_client_line(int client_sock, const char* line){
    int c_idx = get_client_index_by_socket(client_sock);
    if (c_idx<0) return;
    int c_id = client_ids[c_idx];

    printf("< %s\n", line);

    // REQ_CONN(LocId)
    if (strncmp(line,"REQ_CONN(",9)==0) {
        int loc = atoi(line+9);
        client_locs[c_idx] = loc;
        printf("Client %d added (Loc %d)\n", c_id, loc);
        char resp[BUFFER_SIZE];
        snprintf(resp,sizeof(resp),"RES_CONN(%d)\n", c_id);
        send(client_sock, resp, strlen(resp), 0);
        return;
    }

    // Se SU
    if (is_su) {
        // REQ_USRADD UID is_spec
        if (strncmp(line,"REQ_USRADD ",11)==0) {
            char tmp[BUFFER_SIZE];
            strcpy(tmp, line+11);
            char* uid     = strtok(tmp," ");
            char* sIsSpec = strtok(NULL," ");
            if (!uid || strlen(uid)!=10 || !sIsSpec) {
                send(client_sock,"ERROR(17)\n",10,0);
                return;
            }
            int isSpec = atoi(sIsSpec);
            int idx = find_su_user(uid);
            if (idx>=0) {
                // update
                su_users[idx].is_special = isSpec;
                char r[BUFFER_SIZE];
                snprintf(r,sizeof(r),"OK(03) %s\n", uid);
                send(client_sock, r, strlen(r), 0);
            } else {
                if (su_count>=MAX_USERS) {
                    send(client_sock,"ERROR(17)\n",10,0);
                } else {
                    strcpy(su_users[su_count].uid, uid);
                    su_users[su_count].is_special=isSpec;
                    su_count++;
                    char r[BUFFER_SIZE];
                    snprintf(r,sizeof(r),"OK(02) %s\n", uid);
                    send(client_sock, r, strlen(r), 0);
                }
            }
        }
        // REQ_USRACCESS UID in/out
        else if (strncmp(line,"REQ_USRACCESS ",14)==0) {
            char tmp[BUFFER_SIZE];
            strcpy(tmp, line+14);
            char* uid = strtok(tmp," ");
            char* dir = strtok(NULL," ");
            if (!uid || strlen(uid)!=10 || !dir) {
                send(client_sock,"ERROR(18)\n",10,0);
                return;
            }
            int idx = find_su_user(uid);
            if (idx<0) {
                send(client_sock,"ERROR(18)\n",10,0);
                return;
            }
            int loc = (strcmp(dir,"in")==0)
                      ? client_locs[c_idx] : -1;

            if (peer_sockets[0]==-1) {
                // sem peer
                char r[BUFFER_SIZE];
                snprintf(r,sizeof(r),"RES_USRACCESS(-1)\n");
                send(client_sock, r, strlen(r),0);
            } else {
                // Mapeia quem fez a requisição
                su_uar_add(uid, client_sock);
                // Manda REQ_LOCREG <UID> <loc>
                char req[BUFFER_SIZE];
                snprintf(req,sizeof(req),
                         "REQ_LOCREG %s %d\n", uid, loc);
                send(peer_sockets[0], req, strlen(req), 0);
            }
        }
        // REQ_DISC(...)
        else if (strncmp(line,"REQ_DISC(",9)==0) {
            close_and_remove_client(client_sock);
            char r[BUFFER_SIZE];
            snprintf(r,sizeof(r),"OK(01)\n");
            send(client_sock,r,strlen(r),0);
        }
        else {
            // Desconhecido
            send(client_sock,"UNKNOWN_CMD\n",12,0);
        }
    }
    else {
        // Se SL
        // REQ_USRLOC <UID>
        if (strncmp(line,"REQ_USRLOC ",11)==0) {
            const char* uid=line+11;
            if (strlen(uid)!=10) {
                send(client_sock,"ERROR(18)\n",10,0);
                return;
            }
            int idx = find_sl_record(uid);
            if (idx<0 || sl_records[idx].location==-1) {
                send(client_sock,"ERROR(18)\n",10,0);
            } else {
                char r[BUFFER_SIZE];
                snprintf(r,sizeof(r),"RES_USRLOC(%d)\n", sl_records[idx].location);
                send(client_sock,r,strlen(r),0);
            }
        }
        // REQ_LOCLIST <UID> <locId> => "inspect"
        else if (strncmp(line,"REQ_LOCLIST ",12)==0) {
            char tmp[BUFFER_SIZE];
            strcpy(tmp,line+12);
            char* uid = strtok(tmp," ");
            char* sLoc= strtok(NULL," ");
            if (!uid || strlen(uid)!=10 || !sLoc) {
                send(client_sock,"ERROR(19)\n",10,0);
                return;
            }
            int locId = atoi(sLoc);

            // Se já há um "inspect" pendente, rejeita
            if (g_pending_inspect_in_use) {
                // recusar outro "inspect" simultâneo
                send(client_sock,"ERROR(19)\n",10,0);
                return;
            }
            g_pending_inspect_in_use=1;
            strcpy(g_pending_inspect_uid, uid);
            g_pending_inspect_loc = locId;
            g_pending_inspect_sock= client_sock;

            if (peer_sockets[0]==-1) {
                // sem SU => permission denied
                g_pending_inspect_in_use=0;
                send(client_sock,"ERROR(19)\n",10,0);
                return;
            }
            // Manda REQ_USRAUTH(UID)
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),
                     "REQ_USRAUTH %s\n", uid);
            send(peer_sockets[0], msg, strlen(msg),0);
        }
        // REQ_DISC(...)
        else if (strncmp(line,"REQ_DISC(",9)==0) {
            close_and_remove_client(client_sock);
            char r[BUFFER_SIZE];
            snprintf(r,sizeof(r),"OK(01)\n");
            send(client_sock,r,strlen(r),0);
        }
        else {
            send(client_sock,"UNKNOWN_CMD\n",12,0);
        }
    }
}

// ----------------------------------------------------
// Mensagens de peer
void handle_peer_message(int peer_sock){
    char buffer[BUFFER_SIZE];
    int valread = recv(peer_sock, buffer, sizeof(buffer)-1,0);
    if(valread<=0){
        printf("Peer %d disconnected\n", peer_id);
        close(peer_sock);
        peer_sockets[0] = -1;
        peer_count--;
        printf("No peer found, starting to listen...\n");
        return;
    }
    buffer[valread]='\0';

    char* line = strtok(buffer,"\n");
    while(line){
        process_peer_line(peer_sock, line);
        line = strtok(NULL,"\n");
    }
}

void process_peer_line(int peer_sock, const char* line){
    // printf("[PEER] %s\n", line);

    // REQ_DISCPEER => peer quer fechar
    if(strncmp(line,"REQ_DISCPEER",12)==0){
        send(peer_sock,"OK(01)\n",7,0);
        close(peer_sock);
        peer_sockets[0] = -1;
        peer_count=0;
        return;
    }
    // Se "OK(01)" => peer confirm disc
    if(strncmp(line,"OK(01)",6)==0){
        close(peer_sock);
        peer_sockets[0] = -1;
        peer_count=0;
        return;
    }
    if(strncmp(line,"ERROR(01)",9)==0){
        fprintf(stderr,"Peer limit exceeded\n");
        exit(0);
    }

    if (is_su) {
        // SU
        // Ao chegar "REQ_LOCREG <UID> <loc>"
        if(strncmp(line,"REQ_LOCREG ",10)==0){
            char tmp[BUFFER_SIZE];
            strcpy(tmp, line+10);
            // char uid[11];
            // int loc=-1;
        }
        // Ao chegar "RES_LOCREG <UID> <oldLoc>"
        else if(strncmp(line,"RES_LOCREG ",10)==0){
            char tmp[BUFFER_SIZE];
            strcpy(tmp,line+10);
            char uid[11];
            int oldLoc=-1;
            if(sscanf(tmp,"%10s %d", uid, &oldLoc)==2){
                int c_sock = su_uar_remove(uid);
                if(c_sock>0){
                    char r[BUFFER_SIZE];
                    snprintf(r,sizeof(r),"RES_USRACCESS(%d)\n", oldLoc);
                    send(c_sock, r, strlen(r),0);
                }
            }
        }
        // Ao chegar "REQ_USRAUTH <UID>"
        else if(strncmp(line,"REQ_USRAUTH ",11)==0){
            char uid[11];
            if(sscanf(line+11,"%10s", uid)==1){
                int idx = find_su_user(uid);
                int spec = 0;
                if(idx>=0) {
                    spec = su_users[idx].is_special; 
                } 
                // "RES_USRAUTH(x)"
                // x=1 se tem perm especial, x=0 senão
                char resp[BUFFER_SIZE];
                snprintf(resp,sizeof(resp),"RES_USRAUTH(%d)\n", spec);
                send(peer_sockets[0], resp, strlen(resp),0);
            }
        }
    }
    else {
        // SL
        // Ao chegar "REQ_LOCREG <UID> <loc>" => mas esse vem do SU p/ SL
        if(strncmp(line,"REQ_LOCREG ",10)==0){
            char tmp[BUFFER_SIZE];
            strcpy(tmp, line+10);
            char uid[11];
            int loc=-1;
            if(sscanf(tmp,"%10s %d", uid, &loc)==2){
                int idx = find_sl_record(uid);
                if(idx<0){
                    // Novo
                    if(sl_count>=MAX_USERS){
                        char msg[BUFFER_SIZE];
                        snprintf(msg,sizeof(msg),
                                 "RES_LOCREG %s -1\n", uid);
                        send(peer_sockets[0], msg, strlen(msg),0);
                    } else {
                        strcpy(sl_records[sl_count].uid, uid);
                        sl_records[sl_count].location=loc;
                        sl_count++;
                        char msg[BUFFER_SIZE];
                        snprintf(msg,sizeof(msg),
                                 "RES_LOCREG %s -1\n", uid);
                        send(peer_sockets[0], msg, strlen(msg),0);
                    }
                } else {
                    int oldLoc = sl_records[idx].location;
                    sl_records[idx].location = loc;
                    char msg[BUFFER_SIZE];
                    snprintf(msg,sizeof(msg),
                             "RES_LOCREG %s %d\n", uid, oldLoc);
                    send(peer_sockets[0], msg, strlen(msg),0);
                }
            }
        }
        // Ao chegar "RES_USRAUTH(x)" => sem UID
        else if(strncmp(line,"RES_USRAUTH(",12)==0){
            // parse "RES_USRAUTH(1)" ou "RES_USRAUTH(0)"
            int x=-1;
            if(sscanf(line,"RES_USRAUTH(%d)", &x)==1){
                if(!g_pending_inspect_in_use){
                    // Nao havia "inspect" pendente => ignore
                    return;
                }
                // Tem pending
                g_pending_inspect_in_use=0;
                int c_sock = g_pending_inspect_sock;
                int locId  = g_pending_inspect_loc;
                if(x==0){
                    // permission denied
                    send(c_sock,"ERROR(19)\n",10,0);
                } else {
                    // Montar a lista de quem esta em locId
                    char list[BUFFER_SIZE];
                    list[0]='\0';
                    int found=0;
                    for(int i=0;i<sl_count;i++){
                        if(sl_records[i].location==locId){
                            if(found) strcat(list,", ");
                            strcat(list, sl_records[i].uid);
                            found=1;
                        }
                    }
                    if(!found){
                        strcpy(list,"EMPTY");
                    }
                    char resp[BUFFER_SIZE];
                    snprintf(resp,sizeof(resp),
                             "RES_LOCLIST %s\n", list);
                    send(c_sock, resp, strlen(resp),0);
                }
            }
        }
    }
}

// ----------------------------------------------------
int main(int argc,char* argv[]){
    if(argc!=3){
        fprintf(stderr,"USAGE: %s <PeerPort=40000> <ClientPort=50000|60000>\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    int peer_port   = atoi(argv[1]);
    int client_port = atoi(argv[2]);

    if(client_port==50000){
        is_su=1;
        // printf("[MODE] Running as SU (Usuarios)\n");
        next_client_id = su_next_client_id;
        next_peer_id   = su_next_peer_id;
    }
    else if(client_port==60000){
        is_su=0;
        // printf("[MODE] Running as SL (Localizacao)\n");
        next_client_id = sl_next_client_id;
        next_peer_id   = sl_next_peer_id;
    // } else {
    //     fprintf(stderr,"Invalid port (use 50000=SU ou 60000=SL)\n");
    //     exit(EXIT_FAILURE);
    }

    // Zera arrays
    for(int i=0;i<MAX_PEERS;i++){
        peer_sockets[i]=-1;
    }
    for(int i=0;i<MAX_CLIENTS;i++){
        client_sockets[i]=0;
        client_ids[i]=0;
        client_locs[i]=0;
    }
    // Zeramos base SU/SL
    su_count=0; 
    sl_count=0;
    // Se for SL, zera "global pending"
    g_pending_inspect_in_use=0;
    g_pending_inspect_loc= -1;
    g_pending_inspect_sock=0;

    // 1) peer socket
    int peer_listen_sock = socket(AF_INET6, SOCK_STREAM,0);
    if(peer_listen_sock<0){
        perror("socket peer");
        exit(EXIT_FAILURE);
    }
    int opt=1, no=0;
    setsockopt(peer_listen_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    setsockopt(peer_listen_sock,IPPROTO_IPV6,IPV6_V6ONLY,&no,sizeof(no));

    struct sockaddr_in6 peer_addr6;
    memset(&peer_addr6,0,sizeof(peer_addr6));
    peer_addr6.sin6_family=AF_INET6;
    peer_addr6.sin6_addr   = in6addr_any;
    peer_addr6.sin6_port   = htons(peer_port);

    if(bind(peer_listen_sock,(struct sockaddr*)&peer_addr6,sizeof(peer_addr6))<0){
        if(errno==EADDRINUSE){
            // printf("Peer port in use => connecting as client...\n");
            int connect_sock = socket(AF_INET6, SOCK_STREAM,0);
            if(connect_sock<0){
                perror("[ERRO] socket connect");
                exit(EXIT_FAILURE);
            }
            struct sockaddr_in6 tmp;
            memset(&tmp,0,sizeof(tmp));
            tmp.sin6_family=AF_INET6;
            inet_pton(AF_INET6,"::1",&tmp.sin6_addr);
            tmp.sin6_port=htons(peer_port);

            if(connect(connect_sock,(struct sockaddr*)&tmp,sizeof(tmp))<0){
                perror("[ERRO] connect peer");
                close(connect_sock);
                exit(EXIT_FAILURE);
            }
            peer_sockets[0] = connect_sock;
            peer_count=1;
            send(connect_sock,"REQ_CONNPEER()\n",15,0);
        } else {
            perror("bind peer");
            exit(EXIT_FAILURE);
        }
    } else {
        printf("No peer found, starting to listen...\n");
        if(listen(peer_listen_sock,1)<0){
            perror("listen peer");
            exit(EXIT_FAILURE);
        }
    }

    // 2) client socket
    int server_sock = socket(AF_INET6, SOCK_STREAM,0);
    if(server_sock<0){
        perror("socket client");
        exit(EXIT_FAILURE);
    }
    setsockopt(server_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    setsockopt(server_sock,IPPROTO_IPV6,IPV6_V6ONLY,&no,sizeof(no));

    struct sockaddr_in6 client_addr6;
    memset(&client_addr6,0,sizeof(client_addr6));
    client_addr6.sin6_family=AF_INET6;
    client_addr6.sin6_addr  = in6addr_any;
    client_addr6.sin6_port  = htons(client_port);

    if(bind(server_sock,(struct sockaddr*)&client_addr6,sizeof(client_addr6))<0){
        perror("bind client");
        exit(EXIT_FAILURE);
    }
    if(listen(server_sock,MAX_CLIENTS)<0){
        perror("listen client");
        exit(EXIT_FAILURE);
    }

    // printf("[INFO] Server running. peer_port=%d, client_port=%d\n", peer_port,client_port);

    // Loop principal
    while(1){
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO,&readfds);
        FD_SET(peer_listen_sock,&readfds);
        FD_SET(server_sock,&readfds);

        int max_sd = (peer_listen_sock>server_sock?peer_listen_sock:server_sock);

        for(int i=0;i<MAX_PEERS;i++){
            if(peer_sockets[i]!=-1){
                FD_SET(peer_sockets[i],&readfds);
                if(peer_sockets[i]>max_sd) max_sd=peer_sockets[i];
            }
        }
        for(int i=0;i<MAX_CLIENTS;i++){
            if(client_sockets[i]>0){
                FD_SET(client_sockets[i],&readfds);
                if(client_sockets[i]>max_sd) max_sd=client_sockets[i];
            }
        }

        int activity = select(max_sd+1,&readfds,NULL,NULL,NULL);
        if(activity<0 && errno!=EINTR){
            perror("select");
            continue;
        }

        // Teclado
        if(FD_ISSET(STDIN_FILENO,&readfds)){
            char buf[BUFFER_SIZE];
            if(!fgets(buf,sizeof(buf),stdin)) continue;
            if(strncmp(buf,"kill",4)==0){
                send_req_discpeer_and_exit();
            }
        }
        // peer novo
        if(FD_ISSET(peer_listen_sock,&readfds)){
            int newp = accept(peer_listen_sock,NULL,NULL);
            if(newp<0){
                perror("accept peer");
            } else {
                if(peer_count>=MAX_PEERS){
                    send(newp,"ERROR(01)\n",10,0);
                    printf("Peer limit exceeded\n");
                    close(newp);
                } else {
                    if(peer_sockets[0]!=-1){
                        send(newp,"ERROR(01)\n",10,0);
                        printf("Peer limit exceeded\n");
                        close(newp);
                    } else {
                        peer_sockets[0]=newp;
                        peer_count++;
                        peer_id = next_peer_id;
                        printf("Peer %d connected\n", next_peer_id);
                        char resp[BUFFER_SIZE];
                        snprintf(resp,sizeof(resp),"RES_CONNPEER(%d)\n", next_peer_id);
                        send(newp, resp, strlen(resp),0);
                        if(is_su) su_next_peer_id=++next_peer_id; 
                        else      sl_next_peer_id=++next_peer_id;
                    }
                }
            }
        }
        // client novo
        if(FD_ISSET(server_sock,&readfds)){
            int newc = accept(server_sock,NULL,NULL);
            if(newc<0){
                perror("accept client");
            } else {
                int assigned=0;
                for(int i=0;i<MAX_CLIENTS;i++){
                    if(client_sockets[i]==0){
                        client_sockets[i]=newc;
                        client_ids[i]= next_client_id++;
                        if(is_su) su_next_client_id= next_client_id;
                        else      sl_next_client_id= next_client_id;
                        printf("Client %d connected\n",client_ids[i]);
                        if(is_su) printf("SU New ID: %d\n", client_ids[i]);
                        else      printf("SL New ID: %d\n", client_ids[i]);
                        assigned=1;
                        break;
                    }
                }
                if(!assigned){
                    send(newc,"ERROR(09)\n",10,0);
                    close(newc);
                }
            }
        }
        // peer msgs
        for(int i=0;i<MAX_PEERS;i++){
            if(peer_sockets[i]!=-1 && FD_ISSET(peer_sockets[i],&readfds)){
                handle_peer_message(peer_sockets[i]);
            }
        }
        // client msgs
        for(int i=0;i<MAX_CLIENTS;i++){
            if(client_sockets[i]>0 && FD_ISSET(client_sockets[i],&readfds)){
                handle_client_message(client_sockets[i]);
            }
        }
    }
    return 0;
}
