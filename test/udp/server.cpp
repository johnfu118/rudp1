/*
 * test.cpp
 *
 *  Created on: 2015年9月7日
 *      Author: ryanbai
 */
#include "lwip/tcp.h"
#include "lwip/tcp_impl.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"

#include <sys/socket.h>
//#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int fd = -1;
struct sockaddr_in remaddr;     /* remote address */
socklen_t addrlen = 0;

int main(int argc, const char* argv[])
{
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
	{
		printf("cannot create socket\n");
		return 0;
	}

	struct sockaddr_in myaddr;      /* our address */
	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = inet_addr("10.12.21.11");
	myaddr.sin_port = htons(443);

	if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
			perror("bind failed");
			return 0;
	}

	//初始化
	tcp_init();
	pbuf_init();
	memp_init();

	//
	struct tcp_pcb *listen = tcp_new();
    listen->local_port = 443;
	tcp_listen(listen);

	/* now loop, receiving data and printing what we received */
	const int BUFSIZE = 64*1024;
	char buf[BUFSIZE];
	for (;;) {
		int recvlen = recvfrom(fd, buf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
		if (recvlen > 0) {
			struct pbuf *mybuf = pbuf_alloc(PBUF_TRANSPORT, recvlen, PBUF_POOL);
			if (mybuf == NULL)
			{
				printf("cannot alloc pbuf.\n");
				return -1;
			}

			int copy_len = 0;
			struct pbuf *tmp_buf = mybuf;
			while (tmp_buf != NULL)
			{
				memcpy(tmp_buf->payload, buf+copy_len, mybuf->len);
				copy_len += tmp_buf->len;
				tmp_buf = tmp_buf->next;
			}

			tcp_input(mybuf);
		}

		tcp_tmr();
	}

	return 0;
}


void tcp_timer_needed(void)
{
}


err_t ip_output_if(struct pbuf *p)
{
	return sendto(fd, p->payload, p->len, 0, (struct sockaddr *)&remaddr, addrlen);
}
