#ifndef PRIVATE_H
#define PRIVATE_H

#define BUFFER_SIZE 100

#define TAI_ECGI    130
#define MCC 602
#define MNC 02


#define MAX_WORKER_CORES 8
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define NUM_PORTS_HINT 2
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define BURST_SIZE 32
#define MAX_IP_STRING_LEN 16

struct worker_config {
    unsigned lcore_id;
    uint16_t port_id;  // RX port
    uint16_t tx_port_id; // TX port for forwarding
    uint16_t queue_id;
};

struct readConf
{
    int argc;
    char **argv;
};
#endif
