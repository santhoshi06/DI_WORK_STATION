# Compiler
CC = gcc

# Output binary
TARGET = di_work.exe

# Source files
SRCS = main.c audio.c uart.c websocket.c debug.c

# Object files
OBJS = $(SRCS:.c=.o)

# Libraries
LIBS = -lwinmm -lsetupapi -lwebsockets -lcjson

# Compiler flags
CFLAGS = -Wall

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm *.o *.exe
