CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS =

# Arquivos fontes
COMMON_SRC = common.c
COMMON_OBJ = common.o

CLIENT_SRC = client.c
CLIENT_OBJ = client.o

SERVER_SRC = server.c
SERVER_OBJ = server.o

# Executáveis
CLIENT_EXEC = client
SERVER_EXEC = server

# Regra padrão
all: $(CLIENT_EXEC) $(SERVER_EXEC)

# Compilar o cliente
$(CLIENT_EXEC): $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compilar o servidor
$(SERVER_EXEC): $(SERVER_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compilar arquivos objetos
%.o: %.c
	$(CC) $(CFLAGS) -c $<

# Limpar os arquivos gerados
clean:
	rm -f *.o $(CLIENT_EXEC) $(SERVER_EXEC)