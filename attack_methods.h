#ifndef ATTACK_METHODS_H
#define ATTACK_METHODS_H

#include <pthread.h>

extern volatile int attack_active;

// UDP Methods
void* udp_flood_worker(void* arg);
void* udp_raw_worker(void* arg);

// TCP Methods  
void* tcp_syn_worker(void* arg);
void* tcp_ack_worker(void* arg);

// HTTP Methods
void* http_flood_worker(void* arg);
void* slowloris_worker(void* arg);

// Amplification Methods
void* dns_amp_worker(void* arg);
void* ntp_amp_worker(void* arg);

// Special Methods
void* icmp_flood_worker(void* arg);
void* memcached_amp_worker(void* arg);

// Attack controllers
void start_udp_flood(const char* target, int port, int duration, int threads);
void start_tcp_syn(const char* target, int port, int duration, int threads);
void start_http_flood(const char* target, int port, int duration, int threads);
void start_slowloris(const char* target, int port, int duration, int threads);
void start_dns_amp(const char* target, int duration, int threads);
void start_icmp_flood(const char* target, int duration, int threads);

#endif
