#CROSS_COMPILE ?=aarch64-none-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS=-Wall -Wextra -g
TARGET=aesdsocket
SRC=aesdsocket.c

.PHONY: all clean

all: $(TARGET)

default: all

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o
