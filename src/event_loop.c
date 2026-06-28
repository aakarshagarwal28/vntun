#include<sys/epoll.h>
#include<stdio.h>

int epoll_init(void){
    int epfd = epoll_create1(0); 
    if(epfd < 0){
        perror("epoll_create1"); 
        return -1;
    }
    return epfd; 
}

int epoll_register(int epfd, int interest_fd){
    struct epoll_event ev;
    ev.events = EPOLLIN; 
    ev.data.fd = interest_fd;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, interest_fd, &ev) < 0){
        perror("epoll_ctl"); 
        return -1; 
    }
    return 0; 
}

// rest we can do directly in main


