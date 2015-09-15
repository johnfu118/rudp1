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

struct sockaddr_in remaddr;     /* remote address */
static socklen_t addrlen = sizeof(remaddr);

struct timeval last_ts;
static const unsigned int TIME_INTERVAL = 250000;
struct timeval timeout;
static const int max_loop = 1000;

enum echo_states
{
  ES_NONE = 0,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

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

        udp_process_count++;
        tcp_input(mybuf);
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

    ip_addr_t ip;
    ip.addr = s_addr;
    err_t err = tcp_bind(fd->pcb, &ip, port);
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

    new_fd->state = ES_ACCEPTED;
	setup_pcb(new_fd, newpcb);

    new_fd->retries = 0;
//    fd->p = NULL;
    new_fd->recv_cb = listen_fd->recv_cb;
    /* pass newly allocated fd to our callbacks */
//    ret_err = ERR_OK;

    return listen_fd->accept_cb(new_fd, err);
}

err_t on_recv(void *arg, rudp_pcb tpcb, struct pbuf *p, err_t err)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    err_t ret_err;

    printf("on_recv\n");
    LWIP_ASSERT("arg != NULL",arg != NULL);
    if (p == NULL)
    {
        /* remote host closed connection */
        printf("remote close\n");

        fd->state = ES_CLOSING;

        // LAST-ACK
        // send FIN
        tcp_close(fd->pcb);

        ret_err = fd->recv_cb(fd, NULL, 0, err);

        rudp_free(fd);

//        ret_err = ERR_OK;
        return ret_err;
    }
    else if(err != ERR_OK)
    {
        printf("on_recv err=%d\n", err);
        ret_err = err;
    }
    else if(fd->state == ES_ACCEPTED || fd->state == ES_RECEIVED)
    {
        /* first data chunk in p->payload */
        if (fd->state == ES_ACCEPTED)
            fd->state = ES_RECEIVED;
        /* store reference to incoming pbuf (chain) */
        //    fd->p = p;
        //    echo_send(tpcb, fd);
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
        ret_err = fd->recv_cb(fd, buf, copy_len, err);

        return ret_err;

        //    ret_err = ERR_OK;
    }
    else if(fd->state == ES_CLOSING)
    {
        /* odd case, remote side closing twice, trash data */
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        ret_err = ERR_OK;
    }
    else
    {
        /* unkown fd->state, trash data  */
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        ret_err = ERR_OK;
    }

    ret_err = fd->recv_cb(fd, NULL, 0, err);

    return ret_err;

    //    return ERR_OK;
}

int rudp_connect(rudp_fd_ptr fd, const char* ipaddr, u16_t port, rudp_connected_fn connected_cb, rudp_recv_fn recv_cb)
{
    ip_addr_t s_addr;
    s_addr.addr = inet_addr(ipaddr);
    fd->pcb->local_ip.addr = s_addr.addr;

    fd->connected_cb = connected_cb;
    fd->recv_cb = recv_cb;

    // set udp svr addr
    memset((char *)&remaddr, 0, sizeof(remaddr));
    remaddr.sin_family = AF_INET;
    remaddr.sin_addr.s_addr = s_addr.addr;
    remaddr.sin_port = htons(port);

    return tcp_connect(fd->pcb, &s_addr, port, on_connect);
}

err_t on_connect(void *arg, rudp_pcb tpcb, err_t err)
{
    rudp_fd_ptr fd = (rudp_fd_ptr)arg;
    int ret = fd->connected_cb(fd, err);
    if (ret != 0)
        return ret;

    fd->state = ES_RECEIVED;

    return err;
}

int rudp_send(rudp_fd_ptr fd, const void* buf, size_t len)
{
    // 不处理TCP_WRITE_FLAG_MORE的情况，意味着不能一次性发送大于最大缓冲区的包
    return tcp_write(fd->pcb, buf, len, 1);
}

void rudp_abort(rudp_fd_ptr fd)
{
    tcp_abort(fd->pcb);
}

err_t ip_output_if(struct pbuf *p)
{
    int ret = sendto(udp_fd, p->payload, p->len, 0, (struct sockaddr *)&remaddr, sizeof(remaddr));
    if (ret > 0)
        return ERR_OK;
    perror("udp sendto failed");

    return ret;
}

void
rudp_error(void *arg, err_t err)
{
  rudp_fd_ptr fd = (rudp_fd_ptr)arg;

  printf("%p err=%d\n", arg, err);

  rudp_free(fd);
}

err_t
rudp_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  rudp_fd_ptr fd = (rudp_fd_ptr)arg;
  if (fd != NULL)
  {
      if(fd->state == ES_CLOSING)
      {
          rudp_close(fd);
      }
    ret_err = ERR_OK;
  }
  else
  {
    /* nothing to be done */
    rudp_abort(fd);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}

err_t
rudp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  rudp_fd_ptr fd = (rudp_fd_ptr)arg;

  fd->retries = 0;

//  if(fd->p != NULL)
//  {
//    /* still got pbufs to send */
//    tcp_sent(tpcb, echo_sent);
//    echo_send(tpcb, fd);
//  }
//  else
  {
    /* no more pbufs to send */
    if(fd->state == ES_CLOSING)
    {
      rudp_close(fd);
    }
  }
  return ERR_OK;
}

int
rudp_close(rudp_fd_ptr fd)
{
  return tcp_close(fd->pcb);
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
