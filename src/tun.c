#include<stdio.h> 
#include<stdlib.h> 
#include<string.h> 
#include <unistd.h>        // open(), close()
#include <fcntl.h>         // O_RDWR
#include <sys/ioctl.h>     // ioctl()
#include <net/if.h>        // struct ifreq
#include <linux/if_tun.h>  // IFF_TUN, TUNSETIFF
#include <netinet/ip.h>
#include <arpa/inet.h>
#include "config.h"


int tun_init(void){
    int tun_fd; 
    tun_fd = open("/dev/net/tun", O_RDWR); 

    if (tun_fd < 0) {
        perror("open");
        return -1;
    }

    struct ifreq ifr; 
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, TUN_NAME); 
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    if (ioctl(tun_fd, TUNSETIFF, &ifr) < 0) {
        perror("ioctl");
        return -1;
    }

    return tun_fd; 
}

int tun_config(void){
    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "./scripts/tun_conf.sh %s %s %d",
             TUN_NAME, TUN_IP, TUN_PREFIX);

    int ret = system(cmd);

    if (ret != 0) {
        fprintf(stderr, "Failed to configure TUN interface.\n");
        return 1;
    }

    return 0;
}

ssize_t tun_read(unsigned char *buffer, int tun_fd, size_t buf_size){
    ssize_t n = read(tun_fd, buffer, buf_size);
    return n; 
}

ssize_t tun_write(unsigned char *buffer, int tun_fd, size_t count){
    ssize_t n = write(tun_fd, buffer, count);
    return n; 
}

const struct iphdr *parse_tun_buffer(unsigned char *buffer, size_t n){
    const struct iphdr *ip = (const struct iphdr *) buffer;
    if (n < sizeof(struct iphdr))
        return NULL;
    return ip; 
}

void tun_close(int tun_fd){
    close(tun_fd); 
}


/*
sudo ip addr add 10.99.0.1/24 dev tun0
sudo ip link set tun0 up
*/