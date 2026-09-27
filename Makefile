CC = gcc
CSTD = -std=c99
CFLAGS = -Wall -Wextra -g -O0
LDFLAGS = -fsanitize=address -lz -lcurl

TARGET  = shima
SRCS    = $(wildcard *.c)
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET)
