CC     = gcc
CFLAGS = -Wall -Wextra $(shell pkg-config fuse3 --cflags)
LIBS   = $(shell pkg-config fuse3 --libs)

SRC = src/main.c src/resolve.c src/fs_ops.c src/cow.c src/whiteout.c
INC = -Iinclude

all: mini_unionfs

mini_unionfs: $(SRC)
	$(CC) $(SRC) -o $@ $(INC) $(CFLAGS) $(LIBS)

clean:
	rm -f mini_unionfs

.PHONY: all clean
