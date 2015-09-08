/*
 * client.cpp
 *
 *  Created on: 2015年9月8日
 *      Author: ryanbai
 */
#include "tcp.h"
#include "tcp_impl.h"
#include "memp.h"
#include "pbuf.h"

#include <sys/socket.h>
//#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

err_t connected(void *arg, struct tcp_pcb *tpcb, err_t err);


int fd = -1;
struct sockaddr_in svraddr;      /* server address */

int main(int argc, const char* argv[])
{
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
	{
		printf("cannot create socket\n");
		return 0;
	}

	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&svraddr, 0, sizeof(svraddr));
	svraddr.sin_family = AF_INET;
	svraddr.sin_addr.s_addr = inet_addr("10.12.21.11");
	svraddr.sin_port = htons(443);

	//初始化
	tcp_init();
	pbuf_init();
	memp_init();

	//
	struct tcp_pcb *client = tcp_new();
	ip_addr_t ipaddr;
	ipaddr.addr = svraddr.sin_addr.s_addr;
	tcp_connect(client, &ipaddr, 443, connected);

	/* now loop, receiving data and printing what we received */
	const int BUFSIZE = 64*1024;
	char buf[BUFSIZE];
	for (;;) {
		struct sockaddr_in remaddr;      /* server address */
		socklen_t addrlen;
		int recvlen = recvfrom(fd, buf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
		if (recvlen > 0) {
			struct pbuf *rembuf = pbuf_alloc(PBUF_TRANSPORT, recvlen, PBUF_POOL);
			struct pbuf *mybuf = rembuf;
			if (mybuf == NULL)
			{
				printf("cannot alloc pbuf.\n");
				return -1;
			}

			int copy_len = 0;
			while (mybuf != NULL)
			{
				memcpy(mybuf->payload, buf+copy_len, mybuf->len);
				copy_len += mybuf->len;
				mybuf = mybuf->next;
			}

			tcp_input(rembuf, NULL);
		}

		tcp_tmr();
	}

	return 0;
}


void tcp_timer_needed(void)
{
}


err_t ip_output(struct pbuf *p, struct ip_addr *src, u16_t src_port,
		struct ip_addr *dest, u16_t dest_port, u8_t ttl, u8_t tos, u8_t proto)
{
	return sendto(fd, p->payload, p->len, 0, (struct sockaddr *)&svraddr, sizeof(svraddr));
}

err_t connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	printf("connected\n");
	return ERR_OK;
}


