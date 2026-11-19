#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* sockets */
#endif

#include "udp_out.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
typedef SOCKET sock_t;
#else
typedef int sock_t;
#endif

_Static_assert(sizeof(struct sockaddr_in) <= sizeof(((viture_udp *)0)->addr), "viture_udp.addr too small");

int viture_udp_open(viture_udp *u, const char *host, int port, char *err, size_t errlen)
{
    u->fd = -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        snprintf(err, errlen, "WSAStartup failed");
        return -1;
    }
#endif
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        snprintf(err, errlen, "--host must be an IPv4 address such as 127.0.0.1, got '%.60s'", host);
        goto fail;
    }
    long long fd = (long long)socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if ((SOCKET)fd == INVALID_SOCKET) {
        snprintf(err, errlen, "socket() failed: Winsock error %d", WSAGetLastError());
        goto fail;
    }
#else
    if (fd < 0) {
        snprintf(err, errlen, "socket() failed: %s", strerror(errno));
        goto fail;
    }
#endif
    memcpy(u->addr, &a, sizeof a);
    u->fd = fd;
    return 0;

fail:
#ifdef _WIN32
    WSACleanup();
#endif
    return -1;
}

int viture_udp_send(viture_udp *u, const void *buf, size_t len)
{
    if (u->fd < 0)
        return -1;
    struct sockaddr_in a;
    memcpy(&a, u->addr, sizeof a);
    /* char * and int on Winsock */
    int n = (int)sendto((sock_t)u->fd, (const char *)buf, (int)len, 0, (struct sockaddr *)&a, sizeof a);
    return n == (int)len ? 0 : -1;
}

void viture_udp_close(viture_udp *u)
{
    if (u->fd < 0)
        return;
#ifdef _WIN32
    closesocket((sock_t)u->fd);
    WSACleanup();
#else
    close((sock_t)u->fd);
#endif
    u->fd = -1;
}
