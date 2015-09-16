/*
 * client.cpp
 *
 *  Created on: 2015年9月8日
 *      Author: ryanbai
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "rudp.h"

err_t connected(rudp_fd_ptr fd, err_t err);
void echo_recv(rudp_fd_ptr fd, const void *p, size_t len, err_t err);

bool run_flag = true;
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

    //	tcp_err(client, smtp_tcp_err);
    //	tcp_poll(client, smtp_tcp_poll, SMTP_POLL_INTERVAL);
    //	tcp_sent(client, smtp_tcp_sent);

    ret = rudp_connect(fd, "10.12.21.11", 10001, connected, echo_recv);
    if (ret != 0)
    {
        printf("connect failed\n");
        return 1;
    }

    do {
        rudp_update();
    } while (run_flag);

    return 0;
}

const static char* ECHO_STR = "hello world!";
err_t connected(rudp_fd_ptr fd, err_t err)
{
    printf("connected\n");

    int ret = rudp_send(fd, ECHO_STR, strlen(ECHO_STR));
    if (ret != 0)
    {
        printf("rudp_send failed, ret=%d", ret);
        rudp_close(fd);
        return ret;
    }

    return 0;
}

void
echo_recv(rudp_fd_ptr fd, const void *p, size_t len, err_t err)
{
    assert (err == ERR_OK);
    printf("echo_recv\n");

    if (p != NULL)
    {
        printf("recv %zu\n", len);
        printf("content: %s\n", (char*)p);

        sleep(10);

        int ret = rudp_send(fd, ECHO_STR, strlen(ECHO_STR));
        if (ret != 0)
        {
            printf("rudp_send failed, ret=%d", ret);
            rudp_close(fd);
        }
    }
    else
    {
        printf("remote closed\n");
        // closed
        //      run_flag = false;
        rudp_close(fd);
    }
}
