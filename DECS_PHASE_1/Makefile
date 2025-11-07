# ================================
# Makefile for Proxy Server Project
# ================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2
LDFLAGS  = -pthread
BIN      = proxy_server

SRC      = proxy_server_with_LRU_cache.c proxy_parse.c
OBJ      = $(SRC:.c=.o)

# Default target
all: $(BIN)

# Linking step
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN) $(LDFLAGS)

# Compilation step for each .c file
%.o: %.c proxy_parse.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(BIN) $(OBJ)

# Optional: create tarball for submission
tar:
	tar -cvzf ass1.tgz $(SRC) proxy_parse.h Makefile README

