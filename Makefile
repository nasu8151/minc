CC = gcc
CFLAGS = -Wall -Wextra -std=c99

# Output directory for built binaries
BINDIR := target

# Source directories
SRCDIR_MINCC := mincc
SRCDIR_MINCASM := mincasm

# Binaries
BIN_MINCC := $(BINDIR)/mincc
BIN_MINCASM := $(BINDIR)/mincasm

# Source files
SRCS_MINCC := $(wildcard $(SRCDIR_MINCC)/*.c)
SRCS_MINCASM := $(wildcard $(SRCDIR_MINCASM)/*.c)

# Objexct files
OBJS_MINCC := $(SRCS_MINCC:.c=.o)
OBJS_MINCASM := $(SRCS_MINCASM:.c=.o)

.PHONY: all clean test

all: $(BINDIR) $(BIN_MINCC) $(BIN_MINCASM)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJS_MINCC): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_MINCASM): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_MINCC): $(OBJS_MINCC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS_MINCC)

$(BIN_MINCASM): $(OBJS_MINCASM) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS_MINCASM)

clean:
	rm -rf $(BINDIR)
	rm -f $(SRCDIR_MINCC)/*.o
	rm -f $(SRCDIR_MINCASM)/*.o

test: clean all
	python3 tests/test.py

