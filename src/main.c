#include "udp.h"
#include "config.h"
#include "tun.h"
#include "event_loop.h" 
#include <sys/epoll.h>

int main()
{
    int udp_fd = udp_init(LOCAL_IP, LOCAL_PORT, PEER_IP, PEER_PORT); 

    int tun_fd = tun_init(); 
    tun_config(); 


    struct epoll_event events[MAX_EVENTS]; 
    int epfd = epoll_init(); 
    epoll_register(epfd, tun_fd); 
    epoll_register(epfd, udp_fd); 

    while(1){
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1); 

        if (n < 0) {
            perror("epoll_wait");
            continue;
        }

        short tun_ready = 0, udp_ready = 0; 

        for(int i = 0; i < n; i++){
            if(events[i].data.fd == udp_fd) udp_ready = 1;
            if(events[i].data.fd == tun_fd) tun_ready = 1;
        }

        if(tun_ready){
            // do it's thing
        }

        if(udp_ready){
            // do it's thing
        }

    }

    close(epfd); 
    udp_clean(); 
    tun_close(tun_fd); 
    
    return 0;
}
