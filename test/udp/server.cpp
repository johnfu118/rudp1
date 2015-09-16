/*
 * test.cpp
 *
 *  Created on: 2015年9月7日
 *      Author: ryanbai
 */

#include <stddef.h>

#include "rudp.h"

err_t echo_accept(rudp_fd_ptr fd, err_t err);
void echo_recv(rudp_fd_ptr tpcb, const void* buf, size_t len, err_t err);

int main(int argc, const char* argv[])
{
    int ret = rudp_init();
    if (ret != 0)
        return 1;

    rudp_fd_ptr fd = rudp_socket();
    if (fd == NULL)
    {
        printf("get fd failed\n");
        return 1;
    }

    ret = rudp_bind(fd, "0.0.0.0", 10001);
    if (ret != 0)
        return 1;

    ret = rudp_listen(fd, echo_accept, echo_recv);
    if (ret != 0)
        return 1;

    /* now loop, receiving data and printing what we received */
    for (;;) {
        rudp_update();
    }

    return 0;
}

err_t echo_accept(rudp_fd_ptr fd, err_t err)
{
    printf("accepted\n");
    return 0;
}

void echo_recv(rudp_fd_ptr fd, const void* buf, size_t len, err_t err)
{
    printf("echo_recv\n");
    if (buf != NULL && len != 0)
    {
        printf("recv: %zu\n", len);
        printf("content: %s\n", (char*)buf);

        // echo back
        rudp_send(fd, buf, len);

//        rudp_close(fd);
    }
    else
    {
        printf("remote closed\n");
        rudp_close(fd);
    }
}
