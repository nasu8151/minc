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

.PHONY: all clean test

all: $(BINDIR) $(BIN_MINCC) $(BIN_MINCASM)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BIN_MINCC): $(SRCS_MINCC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS_MINCC)

$(BIN_MINCASM): $(SRCS_MINCASM) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS_MINCASM)

clean:
	rm -rf $(BINDIR)

test: all
	bash tests/run_tests.sh

