CC = gcc
CFLAGS = -Wall -Werror -Wextra -std=c99
SDL2_FLAGS = `sdl2-config --cflags --libs`

TARGET = chip8

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) $(SDL2_FLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
