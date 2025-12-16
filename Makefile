CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread -lrt
TARGETS = parent child
SOURCES = parent.c child.c

.PHONY: all clean debug

all: $(TARGETS)

parent: parent.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

child: child.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) shared_data.bin

debug: CFLAGS += -g -DDEBUG
debug: clean all

run: all
	./parent