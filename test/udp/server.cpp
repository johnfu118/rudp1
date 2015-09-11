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

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
//#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "echo.h"

#include "common.h"

int
init_udp_svr()
{
    int fd = init_udp();
    if (fd < 0)
        return -1;

    struct sockaddr_in myaddr;      /* our address */
    /* bind the socket to any valid IP address and a specific port */
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = inet_addr("10.12.21.11");
    myaddr.sin_port = htons(448);

    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        return -1;
    }

    return 0;
}

int main(int argc, const char* argv[])
{
    int ret = init_udp_svr();
    if (ret != 0)
        return 1;

    //
    init_tcp_stack();

    echo_init();

    /* now loop, receiving data and printing what we received */
    for (;;) {
        update();
    }

    return 0;
}


