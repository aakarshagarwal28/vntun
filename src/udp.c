#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "config.h"

#define BUF_SIZE MBS

int s_fd;
struct sockaddr_in sv;
struct sockaddr_in snd;

int udp_init(char *IPAD, int PRT, char *sndIP, int sndPRT)
{
    s_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (s_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&sv, 0, sizeof(sv));

    sv.sin_family = AF_INET;
    sv.sin_port = htons(PRT);
    inet_pton(AF_INET, IPAD, &sv.sin_addr);

    if (bind(s_fd, (struct sockaddr *)&sv, sizeof(sv)) < 0) {
        perror("bind");
        return 1;
    }

    memset(&snd, 0, sizeof(snd));

    snd.sin_family = AF_INET;
    snd.sin_port = htons(sndPRT);
    inet_pton(AF_INET, sndIP, &snd.sin_addr);

    printf("Established Receive On : %s:%d\n", IPAD, PRT);
    printf("Established Send To    : %s:%d\n", sndIP, sndPRT);

    return s_fd;
}

ssize_t receive_udp_msg(unsigned char *msg,
                        struct sockaddr_in *client_info)
{
    socklen_t len = sizeof(*client_info);

    ssize_t bytes = recvfrom(
        s_fd,
        msg,
        BUF_SIZE,
        0,
        (struct sockaddr *)client_info,
        &len);

    if (bytes < 0) {
        perror("recvfrom");
        return -1;
    }

    return bytes;
}

ssize_t send_udp_msg(const unsigned char *msg, size_t len)
{
    ssize_t sent = sendto(
        s_fd,
        msg,
        len,
        0,
        (struct sockaddr *)&snd,
        sizeof(snd));

    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    return sent;
}

void udp_clean(void)
{
    close(s_fd);
}