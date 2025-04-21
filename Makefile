# Makefile for FTP Client and Server
CC = gcc
CFLAGS = -Wall -Wextra -g
CLIENT_SRC = code/client.c
SERVER_SRC = code/server.c
CLIENT_OUT = client/client
SERVER_OUT = server/server

all: client server

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_OUT) $(CLIENT_SRC)

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_OUT) $(SERVER_SRC)

clean:
	rm -f $(CLIENT_OUT) $(SERVER_OUT)
	rm -rf *.dSYM

# For development and testing
run-client:
	./$(CLIENT_OUT)

run-server:
	./$(SERVER_OUT)

# For quick rebuild and testing
rebuild: clean all

# Install necessary dependencies (if any)
deps:
	@echo "No external dependencies required."

.PHONY: all client server clean run-client run-server rebuild deps