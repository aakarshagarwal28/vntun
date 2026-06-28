#include<stdio.h>
#include<stdlib.h>
#include "udp.h"
#include "config.h"
#include "tun.h"
#include "event_loop.h" 
#include<string.h>
#include <sys/epoll.h>

int udp_fd; 
int tun_fd; 

int handle_tun_event(){
    // called when an IPv4 packet arrives from tun

    /*
    read into tun_buffer using tun.c utility
    */
    unsigned char tun_buffer[MBS]; 
    ssize_t n = tun_read(tun_buffer, tun_fd, MBS); 

    if(n <= 0) {
        perror("tun_read"); 
        return -1; 
    } 

    /*
    check if IPv4 only
    check if destination is 10.99.0.2 otherwise reject it for now. 
    */
    if((tun_buffer[0] >> 4) != 4){
        return -1; 
    }

    const struct iphdr *ip = parse_tun_buffer(tun_buffer, n); 

    if(ip == NULL){
        return -1; 
    }

    char dst[INET_ADDRSTRLEN];  

    if (inet_ntop(AF_INET, &ip->daddr, dst, sizeof(dst)) == NULL) {
        perror("inet_ntop");
        return -1;
    }

    if(strcmp(dst, "10.99.0.2") != 0){
        return -1; 
    }

    /*
    encrypt packet
    attach header
    get the final clean udp payload
    */
    unsigned char enc[MBS]; 
    encrypt(tun_buffer, n, enc); 
    unsigned char payload[n + 8]; 
    ssize_t n1 = attach_header(enc, n, payload); 

    if(n1 < 0){
        perror("header attachment"); 
        return -1; 
    }

    /*
    fire the udp packet to PEER IP, PEER PORT
    */
    
    if(send_udp_msg(payload, n1) < 0){
        return -1; 
    } 


    return 0; 
}

int handle_udp_event(){
    // called when a UDP packet arrives

    /*
    receive udp packet
    */
    struct sockaddr_in client_info; 
    unsigned char payload[MBS]; 
    ssize_t n1 = receive_udp_msg(payload, &client_info);  

    if(n1 <= 8){
        return -1; 
    }
    
    /*
    match the header otherwise reject it
    remove the header 
    decrypt the payload
    */
    if(memcmp(payload, MAGIC_HEADER, 8) != 0){
            return -1; 
    }

    size_t n = n1 - 8; 
    unsigned char enc[n]; 

    memcpy(enc, payload + 8, n); 
    unsigned char dec[n]; 

    encrypt(enc, n, dec); // decrypt

    /*
    format it into a way that on reading to tun, kernel has no issues processing it
    we already ensured it!!
    */

    if(tun_write(dec, tun_fd, n) < 0){
        return -1; 
    } 

    return 0; 
}

int main()
{
    udp_fd = udp_init(LOCAL_IP, LOCAL_PORT, PEER_IP, PEER_PORT); 

    tun_fd = tun_init(); 
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
            handle_tun_event(); 
        }

        if(udp_ready){
            handle_udp_event(); 
        }

    }

    close(epfd); 
    udp_clean(); 
    tun_close(tun_fd); 

    return 0;
}
