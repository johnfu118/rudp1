#include "test_tcp.h"

#include <gtest/gtest.h>
#include <iostream>
using namespace std;

#include "lwip/init.h"
#include "lwip/tcp_impl.h"
#include "lwip/stats.h"
#include "tcp_helper.h"


#ifdef _MSC_VER
#pragma warning(disable: 4307) /* we explicitly wrap around TCP seqnos */
#endif

#if !LWIP_STATS || !TCP_STATS || !MEMP_STATS
#error "This tests needs TCP- and MEMP-statistics enabled"
#endif
#if TCP_SND_BUF <= TCP_WND
#error "This tests needs TCP_SND_BUF to be > TCP_WND"
#endif

static u8_t test_tcp_timer;

/* our own version of tcp_tmr so we can reset fast/slow timer state */
static void
test_tcp_tmr(void)
{
  tcp_fasttmr();
  if (++test_tcp_timer & 1) {
    tcp_slowtmr();
  }
}

class LWIPEnvironment : public testing::Environment
{
public:
    virtual void SetUp()
    {
        lwip_init();
    }
    virtual void TearDown()
    {
    }
};

class LWIPTest : public testing::Test {
 protected:
  void SetUp() {
      /* reset iss to default (6510) */
      tcp_ticks = 0;
      tcp_ticks = 0 - (tcp_next_iss() - 6510);
      tcp_next_iss();
      tcp_ticks = 0;

      test_tcp_timer = 0;
      tcp_remove_all();
  }
  void TearDown() {
      tcp_remove_all();
  }
  // Some expensive resource shared by all tests.
};

/* Test functions */

/** Call tcp_new() and tcp_abort() and test memp stats */
TEST_F(LWIPTest, /*test_tcp_new_abort*/ TCPNew)
{
  struct tcp_pcb* pcb;

  ASSERT_EQ(lwip_stats.memp[MEMP_TCP_PCB].used, 0);

  pcb = tcp_new();
  ASSERT_TRUE(pcb != NULL);
  if (pcb != NULL) {
    ASSERT_EQ(lwip_stats.memp[MEMP_TCP_PCB].used, 1);
    tcp_abort(pcb);
    ASSERT_EQ(lwip_stats.memp[MEMP_TCP_PCB].used, 0);
  }
}
//

/** Create an ESTABLISHED pcb and check if receive callback is called */
TEST_F(LWIPTest, test_tcp_recv_inseq)
{
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf* p;
  char data[] = {1, 2, 3, 4};
  ip_addr_t remote_ip, local_ip;
  u16_t data_len;
  u16_t remote_port = 0x100, local_port = 0x101;
//  struct netif netif;
//  struct test_tcp_txcounters txcounters;


  /* initialize local vars */
  //memset(&netif, 0, sizeof(netif));
//  IP_ADDR4(&local_ip, 192, 168, 1, 1);
//  IP_ADDR4(&remote_ip, 192, 168, 1, 2);
//  IP_ADDR4(&netmask,   255, 255, 255, 0);
//  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
  data_len = sizeof(data);
  /* initialize counter struct */
  memset(&counters, 0, sizeof(counters));
  counters.expected_data_len = data_len;
  counters.expected_data = data;

  /* create and initialize the pcb */
  pcb = test_tcp_new_counters_pcb(&counters);
  ASSERT_TRUE(pcb != NULL);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);

  /* create a segment */
  tcp_create_rx_segment(pcb, counters.expected_data, data_len, 0, 0, 0, &p);
  ASSERT_TRUE(p != NULL);
  if (p != NULL) {
    /* pass the segment to tcp_input */
    tcp_input(remote_ip, remote_port, p);
//    tcp_input(p);
    /* check if counters are as expected */
    ASSERT_TRUE(counters.close_calls == 0);
    ASSERT_TRUE(counters.recv_calls == 1);
    ASSERT_TRUE(counters.recved_bytes == data_len);
    ASSERT_TRUE(counters.err_calls == 0);
  }

  /* make sure the pcb is freed */
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}


extern struct test_tcp_txcounters txcounters;
/** Provoke fast retransmission by duplicate ACKs and then recover by ACKing all sent data.
 * At the end, send more data. */
TEST_F(LWIPTest, test_tcp_fast_retx_recover)
{
//  struct test_tcp_txcounters txcounters;
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf* p;
  char data1[] = { 1,  2,  3,  4};
  char data2[] = { 5,  6,  7,  8};
  char data3[] = { 9, 10, 11, 12};
  char data4[] = {13, 14, 15, 16};
  char data5[] = {17, 18, 19, 20};
  char data6[] = {21, 22, 23, 24};
  ip_addr_t remote_ip, local_ip;
  u16_t remote_port = 0x100, local_port = 0x101;
  err_t err;


  /* initialize local vars */
//  IP_ADDR4(&local_ip,  192, 168,   1, 1);
//  IP_ADDR4(&remote_ip, 192, 168,   1, 2);
//  IP_ADDR4(&netmask,   255, 255, 255, 0);
//  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
  memset(&txcounters, 0, sizeof(txcounters));
  memset(&counters, 0, sizeof(counters));

  /* create and initialize the pcb */
  pcb = test_tcp_new_counters_pcb(&counters);
  ASSERT_TRUE(pcb != NULL);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);
  pcb->mss = TCP_MSS;
  /* disable initial congestion window (we don't send a SYN here...) */
  pcb->cwnd = pcb->snd_wnd;

  /* send data1 */
  err = tcp_write(pcb, data1, sizeof(data1), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  ASSERT_TRUE(txcounters.num_tx_calls == 1);
  ASSERT_TRUE(txcounters.num_tx_bytes == sizeof(data1) + sizeof(struct tcp_hdr)/* + sizeof(struct ip_hdr)*/);
  memset(&txcounters, 0, sizeof(txcounters));
 /* "recv" ACK for data1 */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 4, TCP_ACK, &p);
  ASSERT_TRUE(p != NULL);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(pcb->unacked == NULL);
  /* send data2 */
  err = tcp_write(pcb, data2, sizeof(data2), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  ASSERT_TRUE(txcounters.num_tx_calls == 1);
  ASSERT_TRUE(txcounters.num_tx_bytes == sizeof(data2) + sizeof(struct tcp_hdr));
  memset(&txcounters, 0, sizeof(txcounters));
  /* duplicate ACK for data1 (data2 is lost) */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  ASSERT_TRUE(p != NULL);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(pcb->dupacks == 1);
  /* send data3 */
  err = tcp_write(pcb, data3, sizeof(data3), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* nagle enabled, no tx calls */
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));
  /* 2nd duplicate ACK for data1 (data2 and data3 are lost) */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  ASSERT_TRUE(p != NULL);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(pcb->dupacks == 2);
  /* queue data4, don't send it (unsent-oversize is != 0) */
  err = tcp_write(pcb, data4, sizeof(data4), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  /* 3nd duplicate ACK for data1 (data2 and data3 are lost) -> fast retransmission */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  ASSERT_TRUE(p != NULL);
  tcp_input(remote_ip, remote_port, p);
  /*ASSERT_TRUE(txcounters.num_tx_calls == 1);*/
  ASSERT_TRUE(pcb->dupacks == 3);
  memset(&txcounters, 0, sizeof(txcounters));
  /* TODO: check expected data?*/
  
  /* send data5, not output yet */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  /*err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);*/
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));
  {
    int i = 0;
    do
    {
      err = tcp_write(pcb, data6, TCP_MSS, TCP_WRITE_FLAG_COPY);
      i++;
    }while(err == ERR_OK);
    ASSERT_TRUE(err != ERR_OK);
  }
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /*ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(txcounters.num_tx_bytes == 0);*/
  memset(&txcounters, 0, sizeof(txcounters));

  /* send even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);

  /* send ACKs for data2 and data3 */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 12, TCP_ACK, &p);
  ASSERT_TRUE(p != NULL);
  tcp_input(remote_ip, remote_port, p);
  /*ASSERT_TRUE(txcounters.num_tx_calls == 0);*/

  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);

#if 0
  /* create expected segment */
  p1 = tcp_create_rx_segment(pcb, counters.expected_data, data_len, 0, 0, 0);
  ASSERT_TRUE(p != NULL);
  if (p != NULL) {
    /* pass the segment to tcp_input */
    tcp_input(p);
    /* check if counters are as expected */
    ASSERT_TRUE(counters.close_calls == 0);
    ASSERT_TRUE(counters.recv_calls == 1);
    ASSERT_TRUE(counters.recved_bytes == data_len);
    ASSERT_TRUE(counters.err_calls == 0);
  }
#endif
  /* make sure the pcb is freed */
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}


static u8_t tx_data[TCP_WND*2];

static void
check_seqnos(struct tcp_seg *segs, int num_expected, u32_t *seqnos_expected)
{
  struct tcp_seg *s = segs;
  int i;
  for (i = 0; i < num_expected; i++, s = s->next) {
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(s->tcphdr->seqno == htonl(seqnos_expected[i]));
  }
  ASSERT_TRUE(s == NULL);
}

/** Send data with sequence numbers that wrap around the u32_t range.
 * Then, provoke fast retransmission by duplicate ACKs and check that all
 * segment lists are still properly sorted. */
TEST_F(LWIPTest, test_tcp_fast_rexmit_wraparound)
{
//  struct netif netif;
//  struct test_tcp_txcounters txcounters;
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf* p;
  ip_addr_t remote_ip, local_ip;
  u16_t remote_port = 0x100, local_port = 0x101;
  err_t err;
#define SEQNO1 (0xFFFFFF00 - TCP_MSS)
#define ISS    6510
  u16_t i, sent_total = 0;
  u32_t seqnos[] = {
    SEQNO1,
    SEQNO1 + (1 * TCP_MSS),
    SEQNO1 + (2 * TCP_MSS),
    SEQNO1 + (3 * TCP_MSS),
    SEQNO1 + (4 * TCP_MSS),
    SEQNO1 + (5 * TCP_MSS)};


  for (i = 0; i < sizeof(tx_data); i++) {
    tx_data[i] = (u8_t)i;
  }

  /* initialize local vars */
/*
  IP_ADDR4(&local_ip,  192, 168,   1, 1);
  IP_ADDR4(&remote_ip, 192, 168,   1, 2);
  IP_ADDR4(&netmask,   255, 255, 255, 0);
  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
*/
  memset(&txcounters, 0, sizeof(txcounters));
  memset(&counters, 0, sizeof(counters));

  /* create and initialize the pcb */
  tcp_ticks = SEQNO1 - ISS;
  pcb = test_tcp_new_counters_pcb(&counters);
  ASSERT_TRUE(pcb != NULL);
  ASSERT_TRUE(pcb->lastack == SEQNO1);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);
  pcb->mss = TCP_MSS;
  /* disable initial congestion window (we don't send a SYN here...) */
  pcb->cwnd = 2*TCP_MSS;

  /* send 6 mss-sized segments */
  for (i = 0; i < 6; i++) {
    err = tcp_write(pcb, &tx_data[sent_total], TCP_MSS, TCP_WRITE_FLAG_COPY);
    ASSERT_TRUE(err == ERR_OK);
    sent_total += TCP_MSS;
  }
  check_seqnos(pcb->unsent, 6, seqnos);
  ASSERT_TRUE(pcb->unacked == NULL);
  err = tcp_output(pcb);
  ASSERT_TRUE(txcounters.num_tx_calls == 2);
  ASSERT_TRUE(txcounters.num_tx_bytes == 2 * (TCP_MSS + sizeof(struct tcp_hdr)));
  memset(&txcounters, 0, sizeof(txcounters));

  check_seqnos(pcb->unacked, 2, seqnos);
  check_seqnos(pcb->unsent, 4, &seqnos[2]);

  /* ACK the first segment */
  tcp_create_rx_segment(pcb, NULL, 0, 0, TCP_MSS, TCP_ACK, &p);
  tcp_input(remote_ip, remote_port, p);
  /* ensure this didn't trigger a retransmission */
  ASSERT_TRUE(txcounters.num_tx_calls == 1);
  ASSERT_TRUE(txcounters.num_tx_bytes == TCP_MSS + sizeof(struct tcp_hdr));
  memset(&txcounters, 0, sizeof(txcounters));
  check_seqnos(pcb->unacked, 2, &seqnos[1]);
  check_seqnos(pcb->unsent, 3, &seqnos[3]);

  /* 3 dupacks */
  ASSERT_TRUE(pcb->dupacks == 0);
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(pcb->dupacks == 1);
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(pcb->dupacks == 2);
  /* 3rd dupack -> fast rexmit */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  tcp_input(remote_ip, remote_port, p);
  ASSERT_TRUE(pcb->dupacks == 3);
  /**
   * fast retransmit +1
   * wnd inflate then send more +3
   * so == 4
   */
  ASSERT_TRUE(txcounters.num_tx_calls == 4);
  memset(&txcounters, 0, sizeof(txcounters));
  ASSERT_TRUE(pcb->unsent == NULL);
  check_seqnos(pcb->unacked, 5, &seqnos[1]);

  /* make sure the pcb is freed */
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}


/** Send data with sequence numbers that wrap around the u32_t range.
 * Then, provoke RTO retransmission and check that all
 * segment lists are still properly sorted. */
TEST_F(LWIPTest, test_tcp_rto_rexmit_wraparound)
{
//  struct netif netif;
//  struct test_tcp_txcounters txcounters;
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  ip_addr_t remote_ip, local_ip;
  u16_t remote_port = 0x100, local_port = 0x101;
  err_t err;
#define SEQNO1 (0xFFFFFF00 - TCP_MSS)
#define ISS    6510
  u16_t i, sent_total = 0;
  u32_t seqnos[] = {
    SEQNO1,
    SEQNO1 + (1 * TCP_MSS),
    SEQNO1 + (2 * TCP_MSS),
    SEQNO1 + (3 * TCP_MSS),
    SEQNO1 + (4 * TCP_MSS),
    SEQNO1 + (5 * TCP_MSS)};


  for (i = 0; i < sizeof(tx_data); i++) {
    tx_data[i] = (u8_t)i;
  }

  /* initialize local vars */
/*
  IP_ADDR4(&local_ip,  192, 168,   1, 1);
  IP_ADDR4(&remote_ip, 192, 168,   1, 2);
  IP_ADDR4(&netmask,   255, 255, 255, 0);
  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
*/
  memset(&txcounters, 0, sizeof(txcounters));
  memset(&counters, 0, sizeof(counters));

  /* create and initialize the pcb */
  tcp_ticks = 0;
  tcp_ticks = 0 - tcp_next_iss();
  tcp_ticks = SEQNO1 - tcp_next_iss();
  pcb = test_tcp_new_counters_pcb(&counters);
  ASSERT_TRUE(pcb != NULL);
  ASSERT_TRUE(pcb->lastack == SEQNO1);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);
  pcb->mss = TCP_MSS;
  /* disable initial congestion window (we don't send a SYN here...) */
  pcb->cwnd = 2*TCP_MSS;

  /* send 6 mss-sized segments */
  for (i = 0; i < 6; i++) {
    err = tcp_write(pcb, &tx_data[sent_total], TCP_MSS, TCP_WRITE_FLAG_COPY);
    ASSERT_TRUE(err == ERR_OK);
    sent_total += TCP_MSS;
  }
  check_seqnos(pcb->unsent, 6, seqnos);
  ASSERT_TRUE(pcb->unacked == NULL);
  err = tcp_output(pcb);
  ASSERT_TRUE(txcounters.num_tx_calls == 2);
  ASSERT_TRUE(txcounters.num_tx_bytes == 2 * (TCP_MSS + sizeof(struct tcp_hdr)));
  memset(&txcounters, 0, sizeof(txcounters));

  check_seqnos(pcb->unacked, 2, seqnos);
  check_seqnos(pcb->unsent, 4, &seqnos[2]);

  /* call the tcp timer some times */
  for (i = 0; i < 10; i++) {
    test_tcp_tmr();
    ASSERT_TRUE(txcounters.num_tx_calls == 0);
  }
  /* 11th call to tcp_tmr: RTO rexmit fires */
  test_tcp_tmr();
  ASSERT_TRUE(txcounters.num_tx_calls == 1);
  check_seqnos(pcb->unacked, 1, seqnos);
  check_seqnos(pcb->unsent, 5, &seqnos[1]);

  /* fake greater cwnd */
  pcb->cwnd = pcb->snd_wnd;
  /* send more data */
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  /* check queues are sorted */
  ASSERT_TRUE(pcb->unsent == NULL);
  check_seqnos(pcb->unacked, 6, seqnos);

  /* make sure the pcb is freed */
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}


/** Provoke fast retransmission by duplicate ACKs and then recover by ACKing all sent data.
 * At the end, send more data. */
static void test_tcp_tx_full_window_lost(u8_t zero_window_probe_from_unsent)
{
//  struct netif netif;
//  struct test_tcp_txcounters txcounters;
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf *p;
  ip_addr_t remote_ip, local_ip;
  u16_t remote_port = 0x100, local_port = 0x101;
  err_t err;
  u16_t sent_total, i;
  u8_t expected = 0xFE;

  for (i = 0; i < sizeof(tx_data); i++) {
    u8_t d = (u8_t)i;
    if (d == 0xFE) {
      d = 0xF0;
    }
    tx_data[i] = d;
  }
  if (zero_window_probe_from_unsent) {
    tx_data[TCP_WND] = expected;
  } else {
    tx_data[0] = expected;
  }

  /* initialize local vars */
/*
  IP_ADDR4(&local_ip,  192, 168,   1, 1);
  IP_ADDR4(&remote_ip, 192, 168,   1, 2);
  IP_ADDR4(&netmask,   255, 255, 255, 0);
  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
*/
  memset(&counters, 0, sizeof(counters));
  memset(&txcounters, 0, sizeof(txcounters));

  /* create and initialize the pcb */
  pcb = test_tcp_new_counters_pcb(&counters);
  ASSERT_TRUE(pcb != NULL);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);
  pcb->mss = TCP_MSS;
  /* disable initial congestion window (we don't send a SYN here...) */
  pcb->cwnd = pcb->snd_wnd;

  /* send a full window (minus 1 packets) of TCP data in MSS-sized chunks */
  sent_total = 0;
  if ((TCP_WND - TCP_MSS) % TCP_MSS != 0) {
    u16_t initial_data_len = (TCP_WND - TCP_MSS) % TCP_MSS;
    err = tcp_write(pcb, &tx_data[sent_total], initial_data_len, TCP_WRITE_FLAG_COPY);
    ASSERT_TRUE(err == ERR_OK);
    err = tcp_output(pcb);
    ASSERT_TRUE(err == ERR_OK);
    ASSERT_TRUE(txcounters.num_tx_calls == 1);
    ASSERT_TRUE(txcounters.num_tx_bytes == initial_data_len + sizeof(struct tcp_hdr));
    memset(&txcounters, 0, sizeof(txcounters));
    sent_total += initial_data_len;
  }
  for (; sent_total < (TCP_WND - TCP_MSS); sent_total += TCP_MSS) {
    err = tcp_write(pcb, &tx_data[sent_total], TCP_MSS, TCP_WRITE_FLAG_COPY);
    ASSERT_TRUE(err == ERR_OK);
    err = tcp_output(pcb);
    ASSERT_TRUE(err == ERR_OK);
    ASSERT_TRUE(txcounters.num_tx_calls == 1);
    ASSERT_TRUE(txcounters.num_tx_bytes == TCP_MSS + sizeof(struct tcp_hdr));
    memset(&txcounters, 0, sizeof(txcounters));
  }
  ASSERT_TRUE(sent_total == (TCP_WND - TCP_MSS));

  /* now ACK the packet before the first */
  tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK, &p);
  tcp_input(remote_ip, remote_port, p);
  /* ensure this didn't trigger a retransmission */
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(txcounters.num_tx_bytes == 0);

  ASSERT_TRUE(pcb->persist_backoff == 0);
  /* send the last packet, now a complete window has been sent */
  err = tcp_write(pcb, &tx_data[sent_total], TCP_MSS, TCP_WRITE_FLAG_COPY);
  sent_total += TCP_MSS;
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  ASSERT_TRUE(txcounters.num_tx_calls == 1);
  ASSERT_TRUE(txcounters.num_tx_bytes == TCP_MSS + sizeof(struct tcp_hdr));
  memset(&txcounters, 0, sizeof(txcounters));
  ASSERT_TRUE(pcb->persist_backoff == 0);

  if (zero_window_probe_from_unsent) {
    /* ACK all data but close the TX window */
    tcp_create_rx_segment_wnd(pcb, NULL, 0, 0, TCP_WND, TCP_ACK, 0, &p);
    tcp_input(remote_ip, remote_port, p);
    /* ensure this didn't trigger any transmission */
    ASSERT_TRUE(txcounters.num_tx_calls == 0);
    ASSERT_TRUE(txcounters.num_tx_bytes == 0);
    /**
     * start persist timer in tcp_receive()
     *
     * if (pcb->snd_wnd == 0) {
        if (pcb->persist_backoff == 0) {
          pcb->persist_cnt = 0;
          pcb->persist_backoff = 1;
        }
     */
    ASSERT_TRUE(pcb->persist_backoff == 1);
    ASSERT_EQ(pcb->persist_cnt , 0);
  }

  /* send one byte more (out of window) -> persist timer starts */
  err = tcp_write(pcb, &tx_data[sent_total], 1, TCP_WRITE_FLAG_COPY);
  ASSERT_TRUE(err == ERR_OK);
  err = tcp_output(pcb);
  ASSERT_TRUE(err == ERR_OK);
  ASSERT_TRUE(txcounters.num_tx_calls == 0);
  ASSERT_TRUE(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));
  if (!zero_window_probe_from_unsent) {
    /* no persist timer unless a zero window announcement has been received */
    ASSERT_EQ(pcb->persist_backoff , 0);
  } else {
    ASSERT_EQ(pcb->persist_backoff , 1);

    /* call tcp_timer some more times to let persist timer count up */
    for (i = 0; i < 4; i++) {
      test_tcp_tmr();
      ASSERT_TRUE(txcounters.num_tx_calls == 0);
      ASSERT_TRUE(txcounters.num_tx_bytes == 0);
      ASSERT_EQ(pcb->persist_cnt , i/2+1);
    }

    /* this should trigger the zero-window-probe */
    txcounters.copy_tx_packets = 1;
    test_tcp_tmr();
    ASSERT_EQ(pcb->persist_cnt , 0);
    ASSERT_EQ(pcb->persist_backoff , 2);
    txcounters.copy_tx_packets = 0;
    ASSERT_EQ(txcounters.num_tx_calls , 1);
    ASSERT_TRUE(txcounters.num_tx_bytes == 1 + sizeof(struct tcp_hdr));
    ASSERT_TRUE(txcounters.tx_packets != NULL);
    if (txcounters.tx_packets != NULL) {
      u8_t sent;
      u16_t ret;
      ret = pbuf_copy_partial(txcounters.tx_packets, &sent, 1, sizeof(struct tcp_hdr));
      ASSERT_TRUE(ret == 1);
      ASSERT_TRUE(sent == expected);
    }
    if (txcounters.tx_packets != NULL) {
      pbuf_free(txcounters.tx_packets);
      txcounters.tx_packets = NULL;
    }
  }

  /* make sure the pcb is freed */
  ASSERT_EQ(lwip_stats.memp[MEMP_TCP_PCB].used , 1);
  tcp_abort(pcb);
  ASSERT_TRUE(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}

TEST_F(LWIPTest, test_tcp_tx_full_window_lost_from_unsent)
{
  test_tcp_tx_full_window_lost(1);
}


TEST_F(LWIPTest, test_tcp_tx_full_window_lost_from_unacked)
{
  test_tcp_tx_full_window_lost(0);
}

int main(int argc, char** argv)
{
    testing::AddGlobalTestEnvironment(new LWIPEnvironment);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
