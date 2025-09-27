#include "network_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

int resolve_hostname(const char* hostname, char* ip_buffer) {
    struct addrinfo hints, *res;
    int status;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
        return -1;
    }
    
    struct sockaddr_in *ipv4 = (struct sockaddr_in*)res->ai_addr;
    inet_ntop(AF_INET, &(ipv4->sin_addr), ip_buffer, INET_ADDRSTRLEN);
    
    freeaddrinfo(res);
    return 0;
}

int validate_ip(const char* ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

int validate_port(int port) {
    return (port > 0 && port <= 65535);
}

void print_network_info(const char* target, int port) {
    char ip[INET_ADDRSTRLEN];
    
    if(validate_ip(target)) {
        strcpy(ip, target);
        printf("Target IP: %s\n", ip);
    } else {
        if(resolve_hostname(target, ip) == 0) {
            printf("Resolved %s -> %s\n", target, ip);
        } else {
            printf("Could not resolve hostname: %s\n", target);
        }
    }
    
    printf("Port: %d\n", port);
    
    // Basic service detection
    if(port == 80) printf("Service: HTTP\n");
    else if(port == 443) printf("Service: HTTPS\n");
    else if(port == 22) printf("Service: SSH\n");
    else if(port == 53) printf("Service: DNS\n");
    else if(port == 25) printf("Service: SMTP\n");
    else printf("Service: Unknown\n");
}
