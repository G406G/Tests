# C&C Server Makefile
# Educational Use Only

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L
LDFLAGS = -lpthread -lcurl
SOURCES = cnc_server.c ascii_art.c network_utils.c ssh_service.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = cnc_server

# Debug build
DEBUG_CFLAGS = -g -DDEBUG

.PHONY: all debug clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo "C&C Server built successfully"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS) users_logins.txt attack_log.txt
	@echo "Clean complete"

install: $(TARGET)
	chmod +x $(TARGET)
	@echo "Installation complete"

# Dependencies
cnc_server.o: cnc_server.c ascii_art.h network_utils.h ssh_service.h
ascii_art.o: ascii_art.c ascii_art.h
network_utils.o: network_utils.c network_utils.h
ssh_service.o: ssh_service.c ssh_service.h
