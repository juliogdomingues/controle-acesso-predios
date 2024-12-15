#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <setjmp.h>

#define MAX_SERVERS 3
#define BUFFER_SIZE 1024
#define TEST_TIMEOUT 5

// Estrutura para controlar os processos dos servidores
typedef struct {
    pid_t pid;
    int peer_port;
    int client_port;
    FILE *logfile;
    char logname[32];
} ServerProcess;

// Variáveis globais
ServerProcess servers[MAX_SERVERS];
int server_count = 0;
jmp_buf test_fail;

// Protótipos das funções
void cleanup(void);
pid_t start_server(int peer_port, int client_port);
int create_client_connection(int port);
void test_peer_connection(void);
void test_client_connection(void);
void test_connection_closure(void);
void test_message_exchange_detailed(void);

// Função para verificar se uma porta está em uso
int is_port_in_use(int port) {
    int sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock < 0) {
        return 1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    return result < 0;
}

// Função para criar conexão com o servidor
int create_client_connection(int port) {
    int sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

// Função para iniciar um servidor
pid_t start_server(int peer_port, int client_port) {
    // Verifica se a porta já está em uso
    if (is_port_in_use(client_port)) {
        printf("Erro: Porta %d já está em uso\n", client_port);
        return -1;
    }

    char logname[32];
    snprintf(logname, sizeof(logname), "server_%d.log", client_port);
    FILE *logfile = fopen(logname, "w+");
    if (!logfile) {
        perror("Failed to create log file");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        fclose(logfile);
        return -1;
    }

    if (pid == 0) { // Processo filho
        dup2(fileno(logfile), STDOUT_FILENO);
        dup2(fileno(logfile), STDERR_FILENO);
        fclose(logfile);

        char peer_port_str[10], client_port_str[10];
        snprintf(peer_port_str, sizeof(peer_port_str), "%d", peer_port);
        snprintf(client_port_str, sizeof(client_port_str), "%d", client_port);
        
        execl("./server", "server", peer_port_str, client_port_str, NULL);
        perror("execl failed");
        exit(1);
    }

    // Processo pai
    servers[server_count].pid = pid;
    servers[server_count].peer_port = peer_port;
    servers[server_count].client_port = client_port;
    servers[server_count].logfile = logfile;
    strncpy(servers[server_count].logname, logname, sizeof(servers[server_count].logname));
    server_count++;

    // Espera o servidor inicializar
    for (int i = 0; i < 10; i++) {
        sleep(1);
        int test_sock = create_client_connection(client_port);
        if (test_sock >= 0) {
            close(test_sock);
            return pid;
        }
    }

    printf("Erro: Servidor na porta %d não iniciou\n", client_port);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    fclose(logfile);
    remove(logname);
    return -1;
}

// Função para limpar recursos
void cleanup(void) {
    printf("\n=== Limpando recursos ===\n");
    
    for (int i = 0; i < server_count; i++) {
        if (servers[i].pid > 0) {
            kill(servers[i].pid, SIGTERM);
            waitpid(servers[i].pid, NULL, 0);
            if (servers[i].logfile) {
                fclose(servers[i].logfile);
            }
            remove(servers[i].logname);
            printf("Servidor com PID %d finalizado\n", servers[i].pid);
        }
    }
}

// Testa cenário da Tabela 1 - Conexão entre peers
void test_peer_connection(void) {
    printf("\n=== Testando cenário de conexão entre peers (Tabela 1) ===\n");
    
    printf("Iniciando Servidor 1...\n");
    if (start_server(40000, 50000) < 0) {
        printf("Falha ao iniciar Servidor 1\n");
        longjmp(test_fail, 1);
    }
    printf("Servidor 1 iniciado com sucesso\n");
    sleep(2);

    printf("Iniciando Servidor 2...\n");
    if (start_server(40000, 60000) < 0) {
        printf("Falha ao iniciar Servidor 2\n");
        longjmp(test_fail, 1);
    }
    printf("Servidor 2 iniciado com sucesso\n");
    sleep(2);

    printf("Tentando iniciar Servidor 3 (deve falhar)...\n");
    if (start_server(40000, 70000) >= 0) {
        printf("Erro: Servidor 3 não deveria ter iniciado\n");
        longjmp(test_fail, 1);
    }
    printf("Servidor 3 falhou como esperado\n");

    printf("Teste de conexão entre peers completado com sucesso!\n");
}

// Testa cenário da Tabela 2 - Conexão de clientes
void test_client_connection(void) {
    printf("\n=== Testando cenário de conexão de clientes (Tabela 2) ===\n");
    
    int client1 = create_client_connection(50000);
    if (client1 < 0) {
        printf("Erro: Não foi possível conectar o primeiro cliente\n");
        longjmp(test_fail, 1);
    }
    printf("Cliente 1 conectado\n");

    const char *add_user1 = "REQ_USRADD 2021808080 1";
    send(client1, add_user1, strlen(add_user1), 0);
    sleep(1);

    char buffer[BUFFER_SIZE];
    int received = recv(client1, buffer, sizeof(buffer)-1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        printf("Resposta para Cliente 1: %s\n", buffer);
    }

    int client2 = create_client_connection(50000);
    if (client2 < 0) {
        printf("Erro: Não foi possível conectar o segundo cliente\n");
        close(client1);
        longjmp(test_fail, 1);
    }
    printf("Cliente 2 conectado\n");

    const char *add_user2 = "REQ_USRADD 2021808081 0";
    send(client2, add_user2, strlen(add_user2), 0);
    sleep(1);

    received = recv(client2, buffer, sizeof(buffer)-1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        printf("Resposta para Cliente 2: %s\n", buffer);
    }

    close(client1);
    close(client2);
    printf("Teste de conexão de clientes completado com sucesso!\n");
}

// Testa cenário da Tabela 3 - Fechamento de conexões
void test_connection_closure(void) {
    printf("\n=== Testando cenário de fechamento de conexões (Tabela 3) ===\n");
    
    int client1 = create_client_connection(50000);
    int client2 = create_client_connection(50000);
    
    if (client1 < 0 || client2 < 0) {
        printf("Erro: Falha ao conectar clientes\n");
        if (client1 >= 0) close(client1);
        if (client2 >= 0) close(client2);
        longjmp(test_fail, 1);
    }

    printf("Simulando kill do cliente na localização 7...\n");
    close(client2);
    sleep(2);

    printf("Simulando kill do servidor SL...\n");
    for (int i = 0; i < server_count; i++) {
        if (servers[i].client_port == 60000) {
            kill(servers[i].pid, SIGTERM);
            waitpid(servers[i].pid, NULL, 0);
            printf("Servidor SL finalizado\n");
            servers[i].pid = -1;
            break;
        }
    }

    close(client1);
    printf("Teste de fechamento de conexões completado com sucesso!\n");
}

// Testa cenário da Tabela 4 - Troca de mensagens
void test_message_exchange_detailed(void) {
    printf("\n=== Testando cenário de troca de mensagens (Tabela 4) ===\n");
    
    int client = create_client_connection(50000);
    if (client < 0) {
        printf("Erro: Não foi possível conectar o cliente\n");
        longjmp(test_fail, 1);
    }

    struct {
        const char *command;
        const char *expected_response;
    } test_cases[] = {
        {"REQ_USRADD 2021808080 1", "OK"},
        {"REQ_FIND 2021808080", "RES_USRLOC"},
        {"REQ_USRACCESS 2021808080 in", "RES_LOCREG"},
        {"REQ_FIND 2021808080", "RES_USRLOC"},
        {"REQ_USRADD 2021808080 0", "OK"},
        {"REQ_USRACCESS 2021808080 out", "RES_LOCREG"},
        {"REQ_LOCLIST 2021808080 -1", "RES_LOCLIST"}
    };

    char buffer[BUFFER_SIZE];
    int test_passed = 0;
    int test_failed = 0;

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        printf("\nEnviando: %s\n", test_cases[i].command);
        send(client, test_cases[i].command, strlen(test_cases[i].command), 0);
        sleep(1);
        
        int received = recv(client, buffer, sizeof(buffer)-1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            printf("Recebido: %s\n", buffer);
            
            if (strstr(buffer, test_cases[i].expected_response) != NULL) {
                printf("✓ Teste passou\n");
                test_passed++;
            } else {
                printf("✗ Teste falhou - Esperava: %s\n", test_cases[i].expected_response);
                test_failed++;
            }
        }
    }

    close(client);
    printf("\nResumo dos testes de mensagens:\n");
    printf("Testes passados: %d\n", test_passed);
    printf("Testes falhos: %d\n", test_failed);

    if (test_failed > 0) {
        longjmp(test_fail, 1);
    }
}

int main(void) {
    printf("Iniciando testes do servidor...\n");

    struct {
        const char *name;
        int status;
    } test_summary[] = {
        {"Conexão entre peers (Tabela 1)", -1},
        {"Conexão de clientes (Tabela 2)", -1},
        {"Fechamento de conexões (Tabela 3)", -1},
        {"Troca de mensagens (Tabela 4)", -1}
    };
    const int num_tests = sizeof(test_summary)/sizeof(test_summary[0]);

    // Verifica portas em uso
    int ports[] = {40000, 50000, 60000, 70000};
    int ports_in_use = 0;
    
    printf("Verificando portas em uso...\n");
    for (size_t i = 0; i < sizeof(ports)/sizeof(ports[0]); i++) {
        if (is_port_in_use(ports[i])) {
            printf("AVISO: Porta %d já está em uso!\n", ports[i]);
            ports_in_use = 1;
        }
    }
    
    if (ports_in_use) {
        printf("\nERRO: Algumas portas necessárias já estão em uso.\n");
        printf("Por favor, feche outros servidores antes de executar os testes.\n");
        printf("Use o comando 'kill' nos outros terminais para encerrar os servidores.\n\n");
        return 1;
    }

    signal(SIGINT, (void *)cleanup);

    // Executa os testes
    if (setjmp(test_fail) == 0) {
        test_peer_connection();
        test_summary[0].status = 1;
        
        test_client_connection();
        test_summary[1].status = 1;
        
        test_connection_closure();
        test_summary[2].status = 1;
        
        test_message_exchange_detailed();
        test_summary[3].status = 1;
    }
    
    // Limpa recursos
    cleanup();
    
    // Imprime resumo final
    printf("\n=== RESUMO DOS TESTES ===\n");
    printf("Resultados individuais:\n");
    int total_passed = 0;
    for (size_t i = 0; i < num_tests; i++) {
        printf("%s: ", test_summary[i].name);
        if (test_summary[i].status == 1) {
            printf("PASSOU ✓\n");
            total_passed++;
        } else if (test_summary[i].status == 0) {
            printf("FALHOU ✗\n");
        } else {
            printf("NÃO EXECUTADO !\n");
        }
    }
    
    printf("\nResumo geral:\n");
    printf("Total de testes: %d\n", num_tests);
    printf("Testes passados: %d\n", total_passed);
    printf("Testes falhos ou não executados: %d\n", num_tests - total_passed);
    printf("Taxa de sucesso: %.1f%%\n", (float)total_passed/num_tests * 100);
    
    if (total_passed == num_tests) {
        printf("\nTODOS OS TESTES PASSARAM COM SUCESSO! ✓\n");
        return 0;
    } else {
        printf("\nALGUNS TESTES FALHARAM OU NÃO FORAM EXECUTADOS ✗\n");
        return 1;
    }
}