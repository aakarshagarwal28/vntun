#include<stdio.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

int tun_init(void); 
int tun_config(void); 
ssize_t tun_read(unsigned char *buffer, int tun_fd, size_t buf_size); 
ssize_t tun_write(unsigned char *buffer, int tun_fd, size_t count);
const struct iphdr *parse_tun_buffer(unsigned char *buffer, size_t n); 
void tun_close(int tun_fd); 