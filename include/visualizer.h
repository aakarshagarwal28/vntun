#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stddef.h>
#include <netinet/ip.h>

/* ---- colors (ANSI escape codes, used sparingly) ---- */
#define VZ_RESET   "\x1b[0m"
#define VZ_CYAN    "\x1b[36m"
#define VZ_GREEN   "\x1b[32m"
#define VZ_YELLOW  "\x1b[33m"
#define VZ_WHITE   "\x1b[37m"
#define VZ_BOLD    "\x1b[1m"

/* startup */
void visualizer_banner(void);
void visualizer_show_startup_info(const char *local_ip, int local_port,
                                   const char *peer_ip, int peer_port,
                                   const char *tun_name, const char *tun_ip);

/* per event-loop iteration */
void visualizer_event_header(const char *label);

/* per packet
   note: dropped/ignored packets never print anything. the counter
   inside visualizer_packet_begin still advances for every event, so
   a viewer can tell packets were skipped from the numbering gap. */
void visualizer_packet_begin(const char *direction);

/* IPv4 / raw bytes */
void visualizer_show_ipv4(const struct iphdr *ip, const unsigned char *packet, size_t len);
void visualizer_show_raw(const unsigned char *buf, size_t len, const char *label);

/* encryption stage */
void visualizer_show_encryption_start(size_t input_size);
void visualizer_show_encryption_done(const unsigned char *enc, size_t len);

/* header + payload stage */
void visualizer_show_header(const unsigned char *header, size_t header_len);
void visualizer_show_payload(size_t payload_size);

/* outbound UDP */
void visualizer_show_udp_send(const char *ip, int port);
void visualizer_show_udp_sent(void);

/* inbound UDP */
void visualizer_show_udp_receive(const char *ip, int port, size_t len);
void visualizer_show_header_verify(int valid);
void visualizer_show_removing_header(void);
void visualizer_show_decrypting(void);

/* kernel injection (final stage) */
void visualizer_show_kernel_injection(void);

#endif