CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket
