CC ?= gcc
WINCC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -O3 -Wall -Wextra -Iaudio/include
WIN_LDFLAGS ?= -lws2_32 -lwinmm -lole32
LINUX_LDFLAGS ?= -lpulse -lpulse-simple

BIN_DIR = bin
SRC_DIR = audio/src

SENDER_BIN = $(BIN_DIR)/lan-audio-sender
RECEIVER_BIN = $(BIN_DIR)/lan-audio-receiver.exe

.PHONY: all clean sender receiver

all: $(SENDER_BIN) $(RECEIVER_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

sender: $(SENDER_BIN)

$(SENDER_BIN): $(SRC_DIR)/sender_linux.c audio/include/protocol.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LINUX_LDFLAGS) -o $@

receiver: $(RECEIVER_BIN)

$(RECEIVER_BIN): $(SRC_DIR)/receiver_win.c audio/include/protocol.h | $(BIN_DIR)
	$(WINCC) $(CFLAGS) $< $(WIN_LDFLAGS) -o $@

clean:
	rm -rf $(BIN_DIR)
