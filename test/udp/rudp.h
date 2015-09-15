/*
 * rudp.h
 *
 *  Created on: 2015年9月14日
 *      Author: johnfu
 */

#ifndef RUDP_H_
#define RUDP_H_

#include <stddef.h>

#include "lwip/tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcp_pcb* rudp_pcb;
struct rudp_state;
typedef struct rudp_state rudp_fd;
typedef struct rudp_state* rudp_fd_ptr;

typedef err_t (*rudp_accept_fn)(rudp_fd_ptr fd, err_t err);
typedef void (*rudp_recv_fn)(rudp_fd_ptr fd, const void* buf, size_t len, err_t err);
typedef err_t (*rudp_connected_fn)(rudp_fd_ptr fd, err_t err);

struct rudp_state
{
    // for tcp_close may fail, rudp must retry, not app
    u8_t is_closing;
    //  u8_t retries;
    rudp_pcb pcb;

    rudp_recv_fn recv_cb;
    rudp_accept_fn accept_cb;
    rudp_connected_fn connected_cb;
};


int rudp_init();

int rudp_update();

rudp_fd_ptr rudp_socket();

int rudp_bind(rudp_fd_ptr pcb, const char* ipaddr, u16_t port);

int rudp_listen(rudp_fd_ptr pcb, rudp_accept_fn accept_cb, rudp_recv_fn recv_cb);

int rudp_connect(rudp_fd_ptr pcb, const char* ipaddr, u16_t port, rudp_connected_fn connected_cb, rudp_recv_fn recv_cb);

int rudp_send(rudp_fd_ptr pcb, const void *buf, size_t len);

void rudp_close(rudp_fd_ptr fd);

#ifdef __cplusplus
}
#endif

#endif /* RUDP_H_ */
