#ifndef CNC_COMMON_H
#define CNC_COMMON_H

#include <time.h>
#include <pthread.h>

// Bot structure
typedef struct bot_s {
    char id[20];
    char ip[16];
    int port;
    time_t last_seen;
    int socket_fd;
    int active;
    pthread_t thread_id;
} bot_t;

// Attack structure
typedef struct attack_s {
    int id;
    char method[50];
    char target[256];
    int port;
    int duration;
    int threads;
    time_t start_time;
    int active;
    int bot_count;
} attack_t;

// Global variables declarations
extern bot_t bots[];
extern int bot_count;
extern attack_t attacks[];
extern int attack_count;

// Function prototypes for SSH service
bot_t* get_bots_list(void);
int get_bot_count(void);
attack_t* get_attacks_list(void);
int get_attack_count(void);
void start_real_attack(const char* method, const char* target, int port, int duration, int threads);
void stop_real_attack(int attack_id);
void stop_all_real_attacks(void);

#endif
