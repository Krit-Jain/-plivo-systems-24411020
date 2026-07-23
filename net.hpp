/*
 * net.hpp — Non-blocking UDP socket helpers.
 *
 * All sockets are set non-blocking so the event loop never stalls
 * waiting on a single recvfrom while another socket has data ready.
 */

#ifndef NET_HPP
#define NET_HPP

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>

/*
 * Create a non-blocking UDP socket, optionally bound to a port on 127.0.0.1.
 * Returns the file descriptor, or -1 on failure.
 */
inline int create_udp_socket(int bind_port = 0) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    /* Non-blocking mode */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        close(fd);
        return -1;
    }

    if (bind_port > 0) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(static_cast<uint16_t>(bind_port));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind");
            close(fd);
            return -1;
        }
    }
    return fd;
}

/*
 * Send data to localhost:port.  Returns bytes sent or -1.
 */
inline ssize_t send_to(int fd, const uint8_t* data, size_t len, int port) {
    struct sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    return sendto(fd, data, len, 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

/*
 * Non-blocking receive.
 *   > 0 : bytes received
 *     0 : nothing available (EAGAIN / EWOULDBLOCK)
 *    -1 : real error
 */
inline ssize_t recv_nb(int fd, uint8_t* buf, size_t cap) {
    ssize_t n = recvfrom(fd, buf, cap, 0, nullptr, nullptr);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return n;
}

#endif /* NET_HPP */
