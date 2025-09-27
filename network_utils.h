#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

int resolve_hostname(const char* hostname, char* ip_buffer);
int validate_ip(const char* ip);
int validate_port(int port);
void print_network_info(const char* target, int port);

#endif
