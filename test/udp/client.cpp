/*
 * client.cpp
 *
 *  Created on: 2015年9月8日
 *      Author: ryanbai
 */

//#include <arpa/inet.h>
#include <assert.h>
//#include <cygwin/in.h>
//#include <cygwin/socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "lwip/tcp.h"
#include "lwip/tcp_impl.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/init.h"

#include "common.h"

err_t connected(void *arg, struct tcp_pcb *tpcb, err_t err);
err_t
echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

extern struct sockaddr_in remaddr;      /* server address */

bool run_flag = true;
int main(int argc, const char* argv[])
{
    int fd = init_udp();
    if (fd < 0)
        return 1;

    init_tcp_stack();

//	//初始化
//	lwip_init();

	//
	struct tcp_pcb *client = tcp_new();
//	tcp_arg(client, s);
	tcp_recv(client, echo_recv);
//	tcp_err(client, smtp_tcp_err);
//	tcp_poll(client, smtp_tcp_poll, SMTP_POLL_INTERVAL);
//	tcp_sent(client, smtp_tcp_sent);


	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&remaddr, 0, sizeof(remaddr));
	remaddr.sin_family = AF_INET;
	remaddr.sin_addr.s_addr = inet_addr("10.12.21.11");
	remaddr.sin_port = htons(448);

	client->local_ip.addr = remaddr.sin_addr.s_addr;
	ip_addr_t ipaddr;
	ipaddr.addr = remaddr.sin_addr.s_addr;
	tcp_connect(client, &ipaddr, 448, connected);

	do {
	    update();
	} while (run_flag);

	return 0;
}

const static char* ECHO_STR = "hello world!";
err_t connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	printf("connected\n");

	err_t wr_err = tcp_write(tpcb, ECHO_STR, strlen(ECHO_STR), 1);
	if (wr_err != ERR_OK)
	{
	    printf("tcp_write failed, ret=%d", wr_err);
	    tcp_close(tpcb);
	    return wr_err;
	}

	return ERR_OK;
}

err_t
echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  assert (err == ERR_OK);

  if (p != NULL)
  {
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

      tcp_close(tpcb);
  }
  else
  {
      printf("closed\n");
      // closed
      run_flag = false;
  }

  return ERR_OK;
}
