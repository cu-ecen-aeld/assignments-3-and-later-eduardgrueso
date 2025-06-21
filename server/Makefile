CROSS_COMPILE ?=aarch64-none-linux-gnu-
CC ?= $(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror
TARGET=aesdsocket

all: $(TARGET)

default: all

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o $(TARGET)

clean:
	rm -f $(TARGET)
