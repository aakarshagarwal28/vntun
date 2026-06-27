#include "udp.h"

int main()
{
    udp_init("127.0.0.1", 9191, "127.0.0.1", 9191); 

    unsigned char *msg = "hello"; 
    send_udp_msg(msg, sizeof(msg)); 

    printf("Sent UDP message: %s\n", msg); 

    unsigned char rec[1000]; 
    struct sockaddr_in ts; 
    memset(&ts, 0, sizeof(ts)); 
    receive_udp_msg(rec, &ts); 

    printf("Received UDP message: %s\n", rec); 


    return 0;
}
