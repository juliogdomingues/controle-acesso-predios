CC = gcc
CFLAGS = -Wall

# Target padrão compila apenas servidor e cliente
all: server client

# Compilação do servidor
server: server.c
	$(CC) $(CFLAGS) -o server server.c

# Compilação do cliente
client: client.c
	$(CC) $(CFLAGS) -o client client.c

# Target específico para testes
tests: server_tests
	@echo "Compilação dos testes completa. Execute ./server_tests para rodar os testes."

# Compilação dos testes
server_tests: server_tests.c
	$(CC) $(CFLAGS) -o server_tests server_tests.c -pthread

# Target para rodar os testes
run_tests: tests
	./server_tests

# Limpa todos os executáveis
clean:
	rm -f server client server_tests

.PHONY: all clean tests run_tests