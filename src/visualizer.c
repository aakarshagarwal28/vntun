#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "visualizer.h"

/* small pause between stages so the journey feels like a flow, not a dump */
#define STAGE_DELAY_US 120000

static int packet_counter = 0;

/* Packets that get dropped (bad protocol, wrong destination, size too
   small, bad header, ...) should never show up on screen at all -- but
   the counter still needs to move so a viewer can tell from the gap in
   numbering that packets were skipped. So visualizer_packet_begin()
   just records that a packet is "pending"; the actual banner is only
   printed the first time a packet reaches a stage worth showing. */
static int packet_pending = 0;
static char pending_direction[32];

static void pause_stage(void){
    usleep(STAGE_DELAY_US);
}

static void print_hex(const unsigned char *buf, size_t len){
    for(size_t i = 0; i < len; i++){
        printf("%02x ", buf[i]);
        if((i + 1) % 8 == 0)
            printf("\n");
    }
    if(len % 8 != 0)
        printf("\n");
}

/* Prints bytes as characters, swapping anything outside the printable
   ASCII range for '.'. Used for the encrypted preview so it visibly
   reads as scrambled data instead of a neat, readable hex dump. */
static void print_printable(const unsigned char *buf, size_t len){
    for(size_t i = 0; i < len; i++){
        unsigned char c = buf[i];
        putchar((c >= 33 && c <= 126) ? c : '.');
        if((i + 1) % 32 == 0)
            putchar('\n');
    }
    if(len % 32 != 0)
        putchar('\n');
}

/* Reads the destination port out of a TCP/UDP segment that follows an
   IPv4 header. Both protocols keep the destination port at the same
   offset (bytes 2-3 of the segment), so one helper covers both. */
static int get_dest_port(const unsigned char *packet, size_t len, const struct iphdr *ip){
    size_t ip_header_len = ip->ihl * 4;
    size_t port_offset = ip_header_len + 2;

    if(len < port_offset + 2)
        return -1;

    return (packet[port_offset] << 8) | packet[port_offset + 1];
}

static const char *protocol_name(const struct iphdr *ip, const unsigned char *packet, size_t len, char *out, size_t out_size){
    switch(ip->protocol){
        case 6: {
            int port = get_dest_port(packet, len, ip);
            if(port == 22)
                snprintf(out, out_size, "TCP (SSH)");
            else
                snprintf(out, out_size, "TCP");
            break;
        }
        case 17:
            snprintf(out, out_size, "UDP");
            break;
        case 1:
            snprintf(out, out_size, "ICMP");
            break;
        default:
            snprintf(out, out_size, "Unknown");
            break;
    }
    return out;
}

void visualizer_banner(void)
{
    printf(VZ_CYAN VZ_BOLD "\n");
    printf("██╗   ██╗███╗   ██╗████████╗██╗   ██╗███╗   ██╗\n");
    printf("██║   ██║████╗  ██║╚══██╔══╝██║   ██║████╗  ██║\n");
    printf("██║   ██║██╔██╗ ██║   ██║   ██║   ██║██╔██╗ ██║\n");
    printf("╚██╗ ██╔╝██║╚██╗██║   ██║   ██║   ██║██║╚██╗██║\n");
    printf(" ╚████╔╝ ██║ ╚████║   ██║   ╚██████╔╝██║ ╚████║\n");
    printf("  ╚═══╝  ╚═╝  ╚═══╝   ╚═╝    ╚═════╝ ╚═╝  ╚═══╝\n");
    printf(VZ_RESET);

    printf(VZ_WHITE "Layer 3 Packet Tunnel\n" VZ_RESET);
    printf(VZ_YELLOW "Watching packets travel through Linux\n\n" VZ_RESET);
}

void visualizer_show_startup_info(const char *local_ip, int local_port,
                                   const char *peer_ip, int peer_port,
                                   const char *tun_name, const char *tun_ip){
    printf(VZ_WHITE "UDP Receive : " VZ_GREEN "%s:%d\n" VZ_RESET, local_ip, local_port);
    printf(VZ_WHITE "UDP Send    : " VZ_GREEN "%s:%d\n\n" VZ_RESET, peer_ip, peer_port);
    printf(VZ_WHITE "TUN Device  : " VZ_GREEN "%s\n" VZ_RESET, tun_name);
    printf(VZ_WHITE "Address     : " VZ_GREEN "%s\n" VZ_RESET, tun_ip);
    printf(VZ_WHITE "State       : " VZ_GREEN "UP\n\n" VZ_RESET);
    printf(VZ_YELLOW "Listening for packets...\n\n" VZ_RESET);
}

void visualizer_event_header(const char *label){
    printf(VZ_CYAN "== %s ==\n\n" VZ_RESET, label);
}

void visualizer_packet_begin(const char *direction){
    packet_counter++;
    packet_pending = 1;
    strncpy(pending_direction, direction, sizeof(pending_direction) - 1);
    pending_direction[sizeof(pending_direction) - 1] = '\0';
}

/* Prints the "Packet #N" banner the first time this packet actually
   has something worth showing. Dropped packets never call any other
   visualizer_show_* function, so this never fires for them and they
   stay completely silent -- only the counter above remembers them. */
static void show_packet_header_if_pending(void){
    if(!packet_pending)
        return;

    packet_pending = 0;

    printf(VZ_WHITE "------------------------------------------------------------\n" VZ_RESET);
    printf(VZ_BOLD "Packet #%d\n" VZ_RESET, packet_counter);
    printf("Direction : " VZ_YELLOW "%s\n" VZ_RESET, pending_direction);
    printf(VZ_WHITE "------------------------------------------------------------\n\n" VZ_RESET);
}

void visualizer_show_ipv4(const struct iphdr *ip, const unsigned char *packet, size_t len){
    show_packet_header_if_pending();

    char src[INET_ADDRSTRLEN];
    char dst[INET_ADDRSTRLEN];
    char proto[16];

    inet_ntop(AF_INET, &ip->saddr, src, sizeof(src));
    inet_ntop(AF_INET, &ip->daddr, dst, sizeof(dst));
    protocol_name(ip, packet, len, proto, sizeof(proto));

    printf(VZ_CYAN "IPv4 Header\n" VZ_RESET);
    printf("  Source      : %s\n", src);
    printf("  Destination : %s\n", dst);
    printf("  Protocol    : %s\n", proto);
    printf("  TTL         : %d\n", ip->ttl);
    printf("  Header Len  : %d bytes\n", ip->ihl * 4);
    printf("  Total Len   : %d bytes\n\n", ntohs(ip->tot_len));

    pause_stage();
}

void visualizer_show_raw(const unsigned char *buf, size_t len, const char *label){
    show_packet_header_if_pending();

    size_t preview = len < 48 ? len : 48;

    printf(VZ_CYAN "%s\n" VZ_RESET, label);
    print_hex(buf, preview);
    printf("\n");

    pause_stage();
}

void visualizer_show_encryption_start(size_t input_size){
    show_packet_header_if_pending();

    printf(VZ_CYAN "Encryption\n" VZ_RESET);
    printf("Input Size : %zu bytes\n", input_size);
    printf("Applying XOR...\n\n");

    pause_stage();
}

void visualizer_show_encryption_done(const unsigned char *enc, size_t len){
    show_packet_header_if_pending();

    size_t preview = len < 64 ? len : 64;

    printf(VZ_GREEN "Encryption Complete\n" VZ_RESET);
    printf("Encrypted Preview :\n");
    print_printable(enc, preview);
    printf("\n");

    pause_stage();
}

void visualizer_show_header(const unsigned char *header, size_t header_len){
    show_packet_header_if_pending();

    printf(VZ_CYAN "Header\n" VZ_RESET);
    print_hex(header, header_len);
    printf("\n");

    pause_stage();
}

void visualizer_show_payload(size_t payload_size){
    show_packet_header_if_pending();

    printf(VZ_WHITE "Final Payload\n" VZ_RESET);
    printf("Header + Encrypted Packet\n");
    printf("Payload Size : %zu bytes\n\n", payload_size);

    pause_stage();
}

void visualizer_show_udp_send(const char *ip, int port){
    show_packet_header_if_pending();

    printf(VZ_CYAN "UDP Transport\n" VZ_RESET);
    printf("Destination : %s:%d\n", ip, port);
    printf("Sending...\n\n");

    pause_stage();
}

void visualizer_show_udp_sent(void){
    show_packet_header_if_pending();

    printf(VZ_GREEN "Packet Sent\n\n" VZ_RESET);

    pause_stage();
}

void visualizer_show_udp_receive(const char *ip, int port, size_t len){
    show_packet_header_if_pending();

    printf(VZ_CYAN "Incoming UDP Datagram\n" VZ_RESET);
    printf("Source  : %s:%d\n", ip, port);
    printf("Payload : %zu bytes\n\n", len);

    pause_stage();
}

void visualizer_show_header_verify(int valid){
    show_packet_header_if_pending();

    printf(VZ_CYAN "Header Verification\n" VZ_RESET);
    printf("Result : %s\n\n", valid ? VZ_GREEN "VALID" VZ_RESET : VZ_YELLOW "INVALID" VZ_RESET);

    pause_stage();
}

void visualizer_show_removing_header(void){
    show_packet_header_if_pending();

    printf("Removing Header...\n\n");

    pause_stage();
}

void visualizer_show_decrypting(void){
    show_packet_header_if_pending();

    printf("Decrypting...\n\n");

    pause_stage();
}

void visualizer_show_kernel_injection(void){
    show_packet_header_if_pending();

    printf(VZ_CYAN "Kernel Injection\n" VZ_RESET);
    printf("write(tun0)\n");
    printf(VZ_GREEN "Packet delivered to Linux networking stack\n\n" VZ_RESET);

    pause_stage();
}