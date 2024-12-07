#!/bin/bash

# Define o caminho raiz do projeto
PROJECT_ROOT="/Users/juliogd/Library/CloudStorage/GoogleDrive-jgdjulio@gmail.com/Meu Drive/CC/2024-2/RC/TP/controle-acesso-predios/v3"

echo "Project root set to: $PROJECT_ROOT"

# Define portas para os servidores e clientes
PEER_PORT=40000
SERVER1_CLIENT_PORT=50000
SERVER2_CLIENT_PORT=60000
SERVER3_CLIENT_PORT=70000

# Função para abrir um novo terminal e executar um comando
open_terminal() {
    osascript <<EOF
tell application "Terminal"
    do script "cd '$PROJECT_ROOT' && $1"
end tell
EOF
}

# Iniciar o primeiro servidor
echo "Starting server 1..."
open_terminal "./server $PEER_PORT $SERVER1_CLIENT_PORT"

# Permitir tempo para inicializar
sleep 2

# Iniciar o segundo servidor
echo "Starting server 2..."
open_terminal "./server $PEER_PORT $SERVER2_CLIENT_PORT"

# Permitir tempo para conectar
sleep 2

# Tentar iniciar o terceiro servidor (deve ser rejeitado)
echo "Attempting to start server 3..."
open_terminal "./server $PEER_PORT $SERVER3_CLIENT_PORT"

# Permitir tempo para o terceiro servidor falhar
sleep 2

# Iniciar um cliente conectado ao primeiro servidor
echo "Starting client 1..."
open_terminal "./client ::1 $SERVER1_CLIENT_PORT $SERVER2_CLIENT_PORT 1"

# Iniciar um segundo cliente conectado ao segundo servidor
echo "Starting client 2..."
open_terminal "./client ::1 $SERVER2_CLIENT_PORT $SERVER1_CLIENT_PORT 2"

echo "Servers and clients started. Interact with the clients to complete tests."