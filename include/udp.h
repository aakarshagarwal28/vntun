#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int udp_init(char *IPAD, int PRT, char *sndIP, int sndPRT); 
ssize_t receive_udp_msg(unsigned char *msg, struct sockaddr_in *client_info);
ssize_t send_udp_msg(const unsigned char *msg, size_t len); 
void udp_clean(void);