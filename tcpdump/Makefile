CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpcap

SRCS = custom_tcpdump.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = custom_tcpdump

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)