#include "udp.h"
#include "config.h"

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

    printf("Received UDP message: %s\n", (char *)rec); 


    return 0;
}
