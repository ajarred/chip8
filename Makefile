.PHONY: all clean

SRCDIR = src
INCDIR = include
OBJDIR = obj

CC = gcc
CFLAGS = -Wall -Wextra -Werror -I$(INCDIR)
SDLFLAGS = `pkg-config sdl3 --cflags --libs`

SRC = $(wildcard $(SRCDIR)/*.c)
OBJ = $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGET = chip8

all: $(TARGET)

debug: CFLAGS+= -DDEBUG
debug: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(SDLFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -f $(TARGET) $(OBJ)
	rm -f $(OBJDIR)/*.o

