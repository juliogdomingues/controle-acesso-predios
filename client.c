#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

#define BUFFER_SIZE 500

void connect_to_server(const char* ip, int port, int* sock);
void read_server_responses(int sock_fd, const char* label);
void read_server_single_line(int sock_fd, const char* label);
void process_response(const char* line, const char* label);

int main(int argc, char* argv[]) {
    if (argc!=5) {
        fprintf(stderr, "Usage: %s <IP> <Port_SU> <Port_SL> <LocID>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char* ip_su = argv[1];
    int port_su = atoi(argv[2]);
    int port_sl = atoi(argv[3]);
    int locId   = atoi(argv[4]);
    if (locId<1 || locId>10) {
        fprintf(stderr, "Invalid argument: LocID must be 1..10\n");
        exit(1);
    }

    int sock_su, sock_sl;
    connect_to_server(ip_su, port_su, &sock_su);
    connect_to_server(ip_su, port_sl, &sock_sl);

    // Envia REQ_CONN(locId) p/ SU e SL
    {
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "REQ_CONN(%d)\n", locId);
        send(sock_su, msg, strlen(msg), 0);
        read_server_responses(sock_su, "SU");

        send(sock_sl, msg, strlen(msg), 0);
        read_server_responses(sock_sl, "SL");
    }

    char command[BUFFER_SIZE];
    while (1) {
        printf("Enter command: ");
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        command[strcspn(command, "\n")] = 0; // remove \n

        if (strncmp(command, "kill", 4)==0) {
            // kill
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_DISC(%d)\n", locId);

            send(sock_su, msg, strlen(msg),0);
            read_server_single_line(sock_su,"SU");

            send(sock_sl, msg, strlen(msg),0);
            read_server_single_line(sock_sl,"SL");


            close(sock_su);
            printf("SU Successful disconnect\n");
            close(sock_sl);
            printf("SU Successful disconnect\n");
            break;
        }

        if (strncmp(command,"add ",4)==0) {
            char* uid = strtok(command+4," ");
            char* sIsSpec = strtok(NULL," ");
            if(!uid || strlen(uid)!=10 || !sIsSpec){
                printf("Usage: add <UID(10)> <0|1>\n");
                continue;
            }
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_USRADD %s %s\n", uid, sIsSpec);
            send(sock_su, msg, strlen(msg),0);
            read_server_single_line(sock_su,"SU");
            continue;
        }
        if(strncmp(command,"in ",3)==0){
            char* uid = strtok(command+3," ");
            if(!uid || strlen(uid)!=10){
                printf("Usage: in <UID(10)>\n");
                continue;
            }
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_USRACCESS %s in\n", uid);
            send(sock_su, msg, strlen(msg),0);
            read_server_single_line(sock_su,"SU");
            continue;
        }
        if(strncmp(command,"out ",4)==0){
            char* uid = strtok(command+4," ");
            if(!uid || strlen(uid)!=10){
                printf("Usage: out <UID(10)>\n");
                continue;
            }
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_USRACCESS %s out\n", uid);
            send(sock_su, msg, strlen(msg),0);
            read_server_single_line(sock_su,"SU");
            continue;
        }
        if(strncmp(command,"find ",5)==0){
            char* uid = strtok(command+5," ");
            if(!uid || strlen(uid)!=10){
                printf("Usage: find <UID(10)>\n");
                continue;
            }
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_USRLOC %s\n", uid);
            send(sock_sl, msg, strlen(msg),0);
            read_server_single_line(sock_sl,"SL");
            continue;
        }
        if(strncmp(command,"inspect ",8)==0){
            char* uid = strtok(command+8," ");
            char* sLoc= strtok(NULL," ");
            if(!uid || strlen(uid)!=10 || !sLoc){
                printf("Usage: inspect <UID(10)> <LOC>\n");
                continue;
            }
            char msg[BUFFER_SIZE];
            snprintf(msg,sizeof(msg),"REQ_LOCLIST %s %s\n", uid, sLoc);
            send(sock_sl, msg, strlen(msg),0);
            read_server_single_line(sock_sl,"SL");
            continue;
        }

        printf("Unknown command.\n");
    }

    return 0;
}

void read_server_responses(int sock_fd, const char* label) {
    char buffer[BUFFER_SIZE+1];

    while (1) {
        int n = read(sock_fd, buffer, BUFFER_SIZE);
        if (n <= 0) {
            break; // sem dados ou socket fechado => sai
        }
        buffer[n] = '\0';
        // processa as linhas
        char* saveptr;
        char* line = strtok_r(buffer, "\n", &saveptr);
        while (line) {
            process_response(line, label);
            line = strtok_r(NULL, "\n", &saveptr);
        }
        // se n < BUFFER_SIZE => provavelmente esvaziou o buffer
        if (n < BUFFER_SIZE) {
            break;
        }
    }
}

void read_server_single_line(int sock_fd, const char* label) {
    char buffer[BUFFER_SIZE+1];
    int n = read(sock_fd, buffer, BUFFER_SIZE);
    if (n > 0) {
        buffer[n] = '\0';
        char* line = strtok(buffer, "\n");
        if (line) {
            process_response(line, label);
        }
    }
}

void process_response(const char* line, const char* label){
    if(!line)return;
    if(strncmp(line,"OK(02)",6)==0){
        // new user
        const char* p = strstr(line," ");
        if(p){
            printf("New user added: %s\n", p+1);
        }
    }
    else if(strncmp(line,"OK(03)",6)==0){
        // updated
        const char* p = strstr(line," ");
        if(p){
            printf("User updated: %s\n", p+1);
        }
    }
    else if(strncmp(line,"ERROR(18)",9)==0){
        printf("User not found\n");
    }
    else if(strncmp(line,"ERROR(19)",9)==0){
        printf("Permission denied\n");
    }
    else if(strncmp(line,"RES_USRLOC(",11)==0){
        int loc=-1;
        sscanf(line,"RES_USRLOC(%d)", &loc);
        printf("Current location: %d\n", loc);
    }
    else if(strncmp(line,"RES_USRACCESS(",14)==0){
        int oldLoc=-1;
        sscanf(line,"RES_USRACCESS(%d)", &oldLoc);
        printf("Ok. Last location: %d\n", oldLoc);
    }
    else if(strncmp(line,"RES_LOCLIST",11)==0){
        // Ex: "RES_LOCLIST 2021808080, 2020909090"
        char* p=strchr(line,' ');
        if(p){
            p++;
            if(strncmp(p,"EMPTY",5)==0){
                printf("No users at the specified location\n");
            } else {
                printf("List of people at the specified location: %s\n", p);
            }
        }
    }
    else if(strncmp(line,"RES_CONN(",9)==0){
        int id=-1;
        if(sscanf(line,"RES_CONN(%d)", &id)==1){
            if (strcmp(label, "SU") == 0) {
                printf("SU New ID: %d\n", id);
            } else if (strcmp(label, "SL") == 0) {
                printf("SL New ID: %d\n", id);
            }
        }
    }
    else if(strncmp(line,"ERROR(09)",9)==0){
        printf("ERROR(09): Maximum client connections reached\n");
    }
    else if(strncmp(line,"ERROR(10)",9)==0){
        printf("ERROR(10): Unknown client ID\n");
    }
    else if(strncmp(line,"ERROR(01)",9)==0){
        printf("ERROR(01): Peer limit reached\n");
    }
    else {
        // debug
        // printf("<< %s\n", line);
    }
}

void connect_to_server(const char* ip, int port, int* sock){
    struct sockaddr_in6 addr6;
    struct sockaddr_in  addr4;
    int is_ipv6 = (strchr(ip,':')!=NULL);

    if((*sock=socket(is_ipv6?AF_INET6:AF_INET, SOCK_STREAM,0))<0){
        perror("socket");
        exit(1);
    }
    if(is_ipv6){
        memset(&addr6,0,sizeof(addr6));
        addr6.sin6_family=AF_INET6;
        addr6.sin6_port  = htons(port);
        if(inet_pton(AF_INET6,ip,&addr6.sin6_addr)<=0){
            perror("inet_pton6");
            close(*sock);
            exit(1);
        }
        if(connect(*sock,(struct sockaddr*)&addr6,sizeof(addr6))<0){
            perror("connect6");
            close(*sock);
            exit(1);
        }
    } else {
        memset(&addr4,0,sizeof(addr4));
        addr4.sin_family=AF_INET;
        addr4.sin_port  = htons(port);
        if(inet_pton(AF_INET,ip,&addr4.sin_addr)<=0){
            perror("inet_pton4");
            close(*sock);
            exit(1);
        }
        if(connect(*sock,(struct sockaddr*)&addr4,sizeof(addr4))<0){
            perror("connect4");
            close(*sock);
            exit(1);
        }
    }
    printf("Connected to server on port %d\n", port);
}
