#include "udp.h"
#include "config.h"
#include "tun.h"

int main()
{
    udp_init(LOCAL_IP, LOCAL_PORT, PEER_IP, PEER_PORT); 

    unsigned char *msg = "hello"; 
    send_udp_msg(msg, sizeof(msg)); 

    printf("Sent UDP message: %s\n", (char *)msg); 

    unsigned char rec[1000]; 
    struct sockaddr_in ts; 
    memset(&ts, 0, sizeof(ts)); 
    receive_udp_msg(rec, &ts); 

    printf("Received UDP message: %s\n\n", (char *)rec); 

    int tun_fd = tun_init(); 
    tun_config(); 

    while(1){
        unsigned char tun_buffer[1000]; 
        ssize_t n = tun_read(tun_buffer, tun_fd, 1000); 
        
        if((tun_buffer[0] >> 4) != 4) continue;

        const struct iphdr *ip = parse_tun_buffer(tun_buffer, n);

        printf("Read %d Bytes from Tun:\n", n); 
        printf("%d\n\n", ip->protocol); 
    }

    return 0;
}
