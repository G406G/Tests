CC = gcc
CFLAGS = -Wall -Wextra -pedantic -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L
LDFLAGS = -lpthread -lcurl
SOURCES = bot.c attack_methods.c ascii_art.c daemon.c killer.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = bot

# Debug build
DEBUG_CFLAGS = -g -DDEBUG

.PHONY: all debug clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo "Bot built successfully"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)
	@echo "Clean complete"

install: $(TARGET)
	chmod +x $(TARGET)
	@echo "Installation complete"

# Dependencies
bot.o: bot.c bot.h attack_methods.h ascii_art.h daemon.h killer.h config.h
attack_methods.o: attack_methods.c attack_methods.h ascii_art.h
ascii_art.o: ascii_art.c ascii_art.h
daemon.o: daemon.c daemon.h config.h
killer.o: killer.c killer.h config.h
