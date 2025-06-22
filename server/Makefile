#CROSS_COMPILE ?=aarch64-none-linux-gnu-
CC ?= gcc
CFLAGS ?= -Wall -Wextra -g
LDFLAGS ?=
TARGET=aesdsocket
SRC=aesdsocket.c

.PHONY: all clean

all: $(TARGET)

default: all

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
