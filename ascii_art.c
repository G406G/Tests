#include "ascii_art.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

void print_banner() {
    printf("\033[1;35m");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  ██████╗ ██████╗ ████████╗██╗  ██╗███████╗██████╗ ███████╗     ║\n");
    printf("║  ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║██╔════╝██╔══██╗██╔════╝     ║\n");
    printf("║  ██████╔╝██████╔╝   ██║   ███████║█████╗  ██████╔╝███████╗     ║\n");
    printf("║  ██╔═══╝ ██╔══██╗   ██║   ██╔══██║██╔══╝  ██╔══██╗╚════██║     ║\n");
    printf("║  ██║     ██║  ██║   ██║   ██║  ██║███████╗██║  ██║███████║     ║\n");
    printf("║  ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚══════╝╕╝  ╚═╝╚══════╝     ║\n");
    printf("║                                                                ║\n");
    printf("║                 [ EDUCATIONAL TESTING TOOL ]                   ║\n");
    printf("║                    [ YOUR SERVERS ONLY ]                       ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
}

void print_attack_header() {
    printf("\033[1;31m");
    printf("┌────────────────────────────────────────────────────────────────┐\n");
    printf("│                    ATTACK IN PROGRESS                          │\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void print_loading(int seconds) {
    printf("\033[1;36mLoading: [");
    fflush(stdout);
    
    int total_ticks = 50;
    for (int i = 0; i < total_ticks; i++) {
        printf("█");
        fflush(stdout);
        usleep(seconds * 1000000 / total_ticks);
    }
    printf("] 100%%\n\033[0m");
}

void print_success() {
    printf("\033[1;32m✓ Operation completed successfully!\033[0m\n");
}

void print_error() {
    printf("\033[1;31m✗ Operation failed!\033[0m\n");
}

void print_stats_header() {
    printf("\033[1;33m");
    printf("┌────────────────────── ATTACK STATISTICS ──────────────────────┐\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}
