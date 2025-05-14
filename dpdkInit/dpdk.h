#ifndef DPDK_H
#define DPDK_H

#include <rte_mbuf.h>
#include <rte_lcore.h>


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 0
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 1024
#define RTE_MAX_ETHPORTS 32

#define MAX_WORKER_LCORES 8


/* Structure Definitions */
typedef struct {
    uint16_t id[RTE_MAX_ETHPORTS];
    uint8_t num_ports;
} PortInfo;

typedef struct {
    PortInfo ports;
    struct rte_mempool *mbuf_pool;
    volatile bool running;
    unsigned worker_lcore_ids[MAX_WORKER_LCORES];
    unsigned num_workers;
} DpdkContext;

int dpdkInit(int argc, char *argv[]);
static int parse_portmask(const char *portmask, PortInfo *ports);
static int configure_port(uint16_t port, const struct rte_mempool *mbuf_pool);
static void print_packet_stats(uint16_t port_id);
static void cleanup_dpdk(DpdkContext *ctx);
__rte_noreturn static void process_packets(DpdkContext *ctx);

#endif
