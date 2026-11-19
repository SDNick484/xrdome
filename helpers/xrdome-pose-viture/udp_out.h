#ifndef XRD_VITURE_UDP_OUT_H
#define XRD_VITURE_UDP_OUT_H

#include <stddef.h>

/* An unconnected UDP socket and the address it sends to. */
typedef struct {
    long long fd; /* a socket (SOCKET on Windows); -1 when closed */
    unsigned char addr[32]; /* a struct sockaddr_in, kept opaque here */
} viture_udp;

/* host is an IPv4 address in dotted form. Returns 0, or -1 with a message. */
int viture_udp_open(viture_udp *u, const char *host, int port, char *err, size_t errlen);

/* Returns 0 when the datagram was handed to the system, -1 otherwise. Nobody
 * listening is not an error the sender sees. */
int viture_udp_send(viture_udp *u, const void *buf, size_t len);

void viture_udp_close(viture_udp *u);

#endif
