#ifndef DPDK_H
#define DPDK_H

#include <rte_mbuf.h>
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 0
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 1024
#define RTE_MAX_ETHPORTS 32

struct port_info
{
	uint16_t num_ports;
	uint16_t id[RTE_MAX_ETHPORTS];
};

int parse_portmask(const char *portmask);
int port_init(uint16_t port, struct rte_mempool *mbuf_pool);
__rte_noreturn void lcore_main(void);
void dpdkInit(int argc, char *argv[]);

#endif
