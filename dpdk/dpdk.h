#ifndef DPDK_H
#define DPDK_H

#include <rte_ethdev.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define NUM_PORTS_HINT 2

typedef struct {
    uint16_t num_ports;
    uint16_t port_ids[RTE_MAX_ETHPORTS];
} dpdk_port_config;

int dpdk_initialize(void);
void dpdk_cleanup(void);
const dpdk_port_config* dpdk_get_port_config(void);
struct rte_mempool* dpdk_get_mempool(void);

#endif // DPDK_H