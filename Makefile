CC=gcc
CFLAGS=-Wall -Werror -g -pthread 
BIN=./bin

PROGS=server-tftp server-chat

.PHONY: all
all: $(PROGS)

LIST=$(addprefix $(BIN)/, $(PROGS))

server-chat: server-chat.c 
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

server-echo: server-echo.c
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

server-echo-connection: server-echo-connection.c
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

client-tftp: client-tftp.c
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

server-tftp: server-tftp/main.c server-tftp/request_handler.c server-tftp/utils.c
	$(CC) -o $(BIN)/$@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(LIST)

zip:
	git archive --format zip --output ${USER}-TP4.zip HEAD
