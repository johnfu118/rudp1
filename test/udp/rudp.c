/*
 * rudp.c
 *
 *  Created on: 2015年9月14日
 *      Author: johnfu
 */

//#include <cygwin/in.h>
//#include <cygwin/socket.h>
#include "rudp.h"

#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <assert.h>


#include "lwip/tcp_impl.h"
#include "lwip/pbuf.h"
#include "lwip/memp.h"

int udp_fd = -1;
int uid = 1;

struct timeval last_ts;
static const unsigned int TIME_INTERVAL = 250000;
struct timeval timeout;
static const int max_loop = 1000;

void
rudp_error(void *arg, err_t err);
err_t
rudp_poll(void *arg, struct tcp_pcb *tpcb);
err_t
rudp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
err_t on_recv(void *arg, rudp_pcb tpcb, struct pbuf *p, err_t err);
err_t on_connect(void *arg, rudp_pcb tpcb, err_t err);
err_t on_accept(void *arg, rudp_pcb newpcb, err_t err);
void rudp_free(rudp_fd_ptr fd);

void init_timer()
{
    gettimeofday(&last_ts, NULL);
}

void tcp_timer()
{
    printf("recvfrom timeout\n");

    //		usleep(250*1000);
    /* timer still needed? */
    if (tcp_active_pcbs || tcp_tw_pcbs)
        tcp_tmr();
}

int bind_udp(in_addr_t s_addr, u16_t port)
{
    struct sockaddr_in myaddr;      /* our address */
    /* bind the socket to any valid IP address and a specific port */
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = s_addr;
    myaddr.sin_port = htons(port);

    if (bind(udp_fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed\n");
        return -1;
    }

    return 0;
}

int rudp_init()
{
    tcp_init();
    pbuf_init();
    memp_init();

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
    {
        perror("cannot create socket\n");
        return -1;
    }

    timeout.tv_sec = 0;
    timeout.tv_usec = TIME_INTERVAL;
    int ret = setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    if (ret != 0)
    {
        perror("setsockopt failed\n");
        return -1;
    }

    init_timer();

    return 0;
}

int rudp_update()
{
    int udp_process_count = 0;
    const int BUFSIZE = 64*1024;
    char buf[BUFSIZE];

    struct sockaddr_in remaddr;     /* remote address */
    static socklen_t addrlen = sizeof(remaddr);

    do
    {
        int recvlen = recvfrom(udp_fd, buf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
        if (recvlen < 0)
        {
            // timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                tcp_timer();
            else
                perror("recvfrom failed\n");
            return -1;
        }

        struct pbuf *mybuf = pbuf_alloc(PBUF_TRANSPORT, recvlen, PBUF_POOL);
        assert (mybuf != NULL);

        int copy_len = 0;
        struct pbuf *tmp_buf = mybuf;
        while (tmp_buf != NULL)
        {
            memcpy(tmp_buf->payload, buf+copy_len, mybuf->len);
            copy_len += tmp_buf->len;
            tmp_buf = tmp_buf->next;
        }

        struct ip_addr_t ipaddr;
        ipaddr.addr = remaddr.sin_addr.s_addr;

        tcp_input(ipaddr, ntohs(remaddr.sin_port), mybuf);

        udp_process_count++;
    } while (udp_process_count < max_loop);

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long long pass_usec = now.tv_sec*1000000 + now.tv_usec - (last_ts.tv_sec*1000000+last_ts.tv_usec);
    if (pass_usec > TIME_INTERVAL)
    {
        last_ts = now;
        tcp_timer();
    }
    else
    {
        // modify block timeout
        timeout.tv_sec = 0;
        timeout.tv_usec = TIME_INTERVAL - pass_usec;
        setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    }

    return 0;
}

void setup_pcb(rudp_fd_ptr fd, rudp_pcb pcb)
{
    tcp_arg(pcb, fd);
    fd->pcb = pcb;

    tcp_recv(fd->pcb, on_recv);
    tcp_err(fd->pcb, rudp_error);
    tcp_poll(fd->pcb, rudp_poll, 0);
    tcp_sent(fd->pcb, rudp_sent);
}

rudp_fd_ptr rudp_socket()
{
    rudp_fd_ptr fd = (rudp_fd_ptr)mem_malloc(sizeof(rudp_fd));
    if (fd == NULL)
        return NULL;

    rudp_pcb pcb = tcp_new();
    if (pcb == NULL)
    {
        mem_free(fd);
        return NULL;
    }
    setup_pcb(fd, pcb);

    return fd;
}

int rudp_bind(rudp_fd_ptr fd, const char *ipaddr, u16_t port)
{
    assert(fd->pcb);
    in_addr_t s_addr = inet_addr(ipaddr);
    int ret = bind_udp(s_addr, port);
    if (ret < 0)
    {
        return ret;
    }

    err_t err = tcp_bind(fd->pcb, port);
    if (err != ERR_OK)
    {
        /* abort? output diagnostic? */
        assert(0);
    }

    return 0;
}

int rudp_listen(rudp_fd_ptr fd, rudp_accept_fn accept_cb, rudp_recv_fn recv_cb)
{
    rudp_pcb listen = tcp_listen(fd->pcb);
    if (listen == NULL)
        return -1;
    fd->pcb = listen;

    fd->recv_cb = recv_cb;
    fd->accept_cb = accept_cb;

    tcp_accept(listen, on_accept);

    tcp_arg(listen, fd);

    return 0;
}

err_t on_accept(void *arg, rudp_pcb newpcb, err_t err)
{

    rudp_fd_ptr listen_fd = (rudp_fd_ptr)arg;

    /* Unless this pcb should have NORMAL priority, set its priority now.
     When running out of pcbs, low priority pcbs can be aborted to create
     new pcbs of higher priority. */
    tcp_setprio(newpcb, TCP_PRIO_MIN);

    rudp_fd_ptr new_fd = (rudp_fd_ptr)mem_malloc(sizeof(rudp_fd));
    if (new_fd == NULL)
    {
        // release newpcb?
        tcp_close(newpcb);
        return ERR_MEM;
    }

    setup_pcb(new_fd, newpcb);

    //    new_fd->retries = 0;
    //    fd->p = NULL;
    new_fd->recv_cb = listen_fd->recv_cb;
    /* pass newly allocated fd to our callbacks */
    //    ret_err = ERR_OK;

    return listen_fd->accept_cb(new_fd, err);
}

/**
  The callback function will be passed a NULL pbuf to
  indicate that the remote host has closed the connection. If
  there are no errors and the callback function is to return
  ERR_OK, then it must free the pbuf. Otherwise, it must not
  free the pbuf so that lwIP core code can store it.
 */
err_t on_recv(void *arg, rudp_pcb tpcb, struct pbuf *p, err_t err)
{
    printf("on_recv\n");

    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    if (fd == NULL)
    {
        if (p == NULL)
        {
            printf("FIN_WAIT_2 fin, ignore\n");
            return ERR_OK;
        }
        else
            assert(0);
    }

    // same as read() ret 0
    /* remote host closed connection */
    if (p == NULL)
    {
        printf("remote close\n");

        fd->recv_cb(fd, NULL, 0, err);

        return ERR_OK;
    }
    // data recved

    // for now, err passed in can only be ERR_OK
    if (err != ERR_OK)
    {
        printf("on_recv err=%d\n", err);
        fd->recv_cb(fd, NULL, 0, err);

        // return ERR_OK means cb execute ok
        // err return is not needed, for up-layer already know
        return ERR_OK;
    }

    const int BUFSIZE = 64*1024;
    char buf[BUFSIZE];
    int copy_len = 0;
    while (p != NULL)
    {
        memcpy(buf+copy_len, p->payload, p->len);
        copy_len += p->len;
        p = p->next;
    }
    printf("recv: %s\n", buf);

    tcp_recved(tpcb, copy_len);

    fd->recv_cb(fd, buf, copy_len, err);

    return ERR_OK;
}

int rudp_connect(rudp_fd_ptr fd, const char* ipaddr, u16_t port, rudp_connected_fn connected_cb, rudp_recv_fn recv_cb)
{
//    fd->pcb->local_ip.addr = s_addr.addr;

    fd->connected_cb = connected_cb;
    fd->recv_cb = recv_cb;

    struct ip_addr_t ip;
    ip.addr = inet_addr(ipaddr);

    return tcp_connect(fd->pcb, &ip, port, on_connect);
}

err_t on_connect(void *arg, rudp_pcb tpcb, err_t err)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    int ret = fd->connected_cb(fd, err);
    if (ret != 0)
        return ret;

    return err;
}

/*
TCP data is sent by enqueueing the data with a call to
tcp_write(). When the data is successfully transmitted to the remote
host, the application will be notified with a call to a specified
callback function.

- err_t tcp_write(struct tcp_pcb *pcb, const void *dataptr, u16_t len,
                  u8_t apiflags)

  Enqueues the data pointed to by the argument dataptr. The length of
  the data is passed as the len parameter. The apiflags can be one or more of:
  - TCP_WRITE_FLAG_COPY: indicates whether the new memory should be allocated
    for the data to be copied into. If this flag is not given, no new memory
    should be allocated and the data should only be referenced by pointer. This
    also means that the memory behind dataptr must not change until the data is
    ACKed by the remote host
  - TCP_WRITE_FLAG_MORE: indicates that more data follows. If this is given,
    the PSH flag is set in the last segment created by this call to tcp_write.
    If this flag is given, the PSH flag is not set.

  The tcp_write() function will fail and return ERR_MEM if the length
  of the data exceeds the current send buffer size or if the length of
  the queue of outgoing segment is larger than the upper limit defined
  in lwipopts.h. The number of bytes available in the output queue can
  be retrieved with the tcp_sndbuf() function.

  The proper way to use this function is to call the function with at
  most tcp_sndbuf() bytes of data. If the function returns ERR_MEM,
  the application should wait until some of the currently enqueued
  data has been successfully received by the other host and try again.
 */
int rudp_send(rudp_fd_ptr fd, const void* buf, size_t len)
{
    // already call close
    if (fd->is_closing)
        return -1;

    // 不处理TCP_WRITE_FLAG_MORE的情况，意味着不能一次性发送大于最大缓冲区的包
    return tcp_write(fd->pcb, buf, len, 1);
}

err_t ip_output_if(struct pbuf *p, struct ip_addr_t remote_ip, u16_t remote_port)
{
    // TODO, how to deal with block? platform dependency!!
    // if ever blocked, tcp_txnow when recover

    struct sockaddr_in remaddr;     /* remote address */
    memset((char *)&remaddr, 0, sizeof(remaddr));
    remaddr.sin_family = AF_INET;
    remaddr.sin_addr.s_addr = remote_ip.addr;
    remaddr.sin_port = htons(remote_port);
    ;
    printf("udp sendto %s:%u\n", inet_ntoa(remaddr.sin_addr), remote_port);

    int ret = sendto(udp_fd, p->payload, p->len, 0, (struct sockaddr *)&remaddr, sizeof(remaddr));
    if (ret > 0)
        return ERR_OK;
    perror("udp sendto failed");

    return ret;
}

/*
If a connection is aborted because of an error, the application is
alerted of this event by the err callback. Errors that might abort a
connection are when there is a shortage of memory. The callback
function to be called is set using the tcp_err() function.

- void tcp_err(struct tcp_pcb *pcb, void (* err)(void *arg,
       err_t err))

  The error callback function does not get the pcb passed to it as a
  parameter since the pcb may already have been deallocated.
 */
void
rudp_error(void *arg, err_t err)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;

    printf("%p err=%d\n", arg, err);

    rudp_free(fd);
}

/**
When a connection is idle (i.e., no data is either transmitted or
received), lwIP will repeatedly poll the application by calling a
specified callback function. This can be used either as a watchdog
timer for killing connections that have stayed idle for too long, or
as a method of waiting for memory to become available. For instance,
if a call to tcp_write() has failed because memory wasn't available,
the application may use the polling functionality to call tcp_write()
again when the connection has been idle for a while.

- void tcp_poll(struct tcp_pcb *pcb,
                err_t (* poll)(void *arg, struct tcp_pcb *tpcb),
                u8_t interval)

  Specifies the polling interval and the callback function that should
  be called to poll the application. The interval is specified in
  number of TCP coarse grained timer shots, which typically occurs
  twice a second. An interval of 10 means that the application would
  be polled every 5 seconds.
 */
err_t
rudp_poll(void *arg, struct tcp_pcb *tpcb)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    if (fd == NULL)
        return ERR_OK;

    if (fd->is_closing)
    {
        // try again
        rudp_close(fd);
    }

    return ERR_OK;
}

/*
  Specifies the callback function that should be called when data has
  successfully been received (i.e., acknowledged) by the remote
  host. The len argument passed to the callback function gives the
  amount bytes that was acknowledged by the last acknowledgment.
 */
err_t
rudp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    if (fd == NULL)
        return ERR_OK;

    if (fd->is_closing)
    {
        // try again
        rudp_close(fd);
    }

    return ERR_OK;
}

/*
  Closes the connection. The function may return ERR_MEM if no memory
  was available for closing the connection. If so, the application
  should wait and try again either by using the acknowledgment
  callback or the polling functionality. If the close succeeds, the
  function returns ERR_OK.

  The pcb is deallocated by the TCP code after a call to tcp_close().
 */
void
rudp_close(rudp_fd_ptr fd)
{
    err_t err = tcp_close(fd->pcb);
    if (err == ERR_OK)
    {
        // fd layer free
        // let tcp layer handle next
        rudp_free(fd);
        return;
    }

    // possible fail
    // try again by using poll or sent cb
    fd->is_closing = 1;
    printf("tcp_close failed, err=%d\n", err);
}

void rudp_free(rudp_fd_ptr fd)
{
    assert (fd != NULL);

    tcp_arg(fd->pcb, NULL);
    tcp_sent(fd->pcb, NULL);
    tcp_recv(fd->pcb, NULL);
    tcp_err(fd->pcb, NULL);
    tcp_poll(fd->pcb, NULL, 0);

    mem_free(fd);
}
