#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include "dpdk.h"
#include "../private.h"

static struct rte_mempool *mbuf_pool = NULL;
static dpdk_port_config port_config = {0};

static int configure_port(uint16_t port, uint16_t num_queues) {
    struct rte_eth_conf port_conf = {0};
    struct rte_eth_dev_info dev_info = {0};
    struct rte_eth_txconf txconf;
    int ret;

    if (!rte_eth_dev_is_valid_port(port)) {
        fprintf(stderr, "Invalid port ID %u\n", port);
        return -EINVAL;
    }

    ret = rte_eth_dev_info_get(port, &dev_info);
    if (ret != 0) {
        fprintf(stderr, "Failed to get device info for port %u: %s\n", port, rte_strerror(ret));
        return ret;
    }

    // Log NIC capabilities
    printf("Port %u: max_rx_queues=%u, max_tx_queues=%u\n",
           port, dev_info.max_rx_queues, dev_info.max_tx_queues);

    // Use single queue
    num_queues = 1;

    // Enable fast mbuf free if supported
    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    // Configure port with single queue
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    ret = rte_eth_dev_configure(port, num_queues, num_queues, &port_conf);
    if (ret != 0) {
        fprintf(stderr, "Failed to configure port %u with %u queues: %s\n", port, num_queues, rte_strerror(ret));
        return ret;
    }

    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    ret = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (ret != 0) {
        fprintf(stderr, "Failed to adjust descriptors for port %u: %s\n", port, rte_strerror(ret));
        return ret;
    }

    // Configure RX queue
    ret = rte_eth_rx_queue_setup(port, 0, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (ret != 0) {
        fprintf(stderr, "RX queue 0 setup failed for port %u: %s\n", port, rte_strerror(ret));
        return ret;
    }
    printf("Configured RX queue 0 for port %u\n", port);

    // Configure TX queue
    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    ret = rte_eth_tx_queue_setup(port, 0, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (ret != 0) {
        fprintf(stderr, "TX queue 0 setup failed for port %u: %s\n", port, rte_strerror(ret));
        return ret;
    }
    printf("Configured TX queue 0 for port %u\n", port);

    // Start port
    ret = rte_eth_dev_start(port);
    if (ret != 0) {
        fprintf(stderr, "Failed to start port %u: %s\n", port, rte_strerror(ret));
        return ret;
    }

    // Wait for link up
    struct rte_eth_link link;
    int retry = 0, max_retries = 10;
    printf("Waiting for port %u link to come up...\n", port);
    do {
        memset(&link, 0, sizeof(link));
        ret = rte_eth_link_get_nowait(port, &link);
        if (ret == 0 && link.link_status == RTE_ETH_LINK_UP) {
            printf("Port %u Link Up - Speed %u Mbps - %s\n",
                   port, link.link_speed, link.link_duplex ? "Full-duplex" : "Half-duplex");
            break;
        }
        if (retry++ >= max_retries) {
            fprintf(stderr, "Warning: Port %u link did not come up after %d attempts\n", port, max_retries);
            break;
        }
        printf("Waiting for port %u link (attempt %d/%d)...\n", port, retry, max_retries);
        rte_delay_ms(1000);
    } while (retry <= max_retries);

    // Enable promiscuous mode
    ret = rte_eth_promiscuous_enable(port);
    if (ret != 0) {
        fprintf(stderr, "Warning: Promiscuous mode enable failed for port %u: %s\n", port, rte_strerror(ret));
    } else {
        printf("Port %u: Promiscuous mode enabled\n", port);
    }

    return 0;
}

void print_port_stats(uint16_t port_id) {
    struct rte_eth_stats stats;
    struct rte_eth_link link;

    if (rte_eth_stats_get(port_id, &stats) == 0) {
        printf("Port %u Stats: RX %"PRIu64" pkts (%"PRIu64" err) | TX %"PRIu64" pkts (%"PRIu64" err)\n",
               port_id, stats.ipackets, stats.ierrors, stats.opackets, stats.oerrors);
    }

    if (rte_eth_link_get_nowait(port_id, &link) == 0) {
        printf("Port %u Link: %s %u Mbps %s\n",
               port_id, link.link_status ? "UP" : "DOWN",
               link.link_speed, link.link_duplex ? "Full" : "Half");
    }
}

int dpdk_initialize(void) {
    // Create memory pool
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                        NUM_MBUFS * NUM_PORTS_HINT,
                                        MBUF_CACHE_SIZE,
                                        0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (!mbuf_pool) {
        fprintf(stderr, "Cannot create mbuf pool: %s\n", rte_strerror(rte_errno));
        return -1;
    }

    // Initialize ports with single queue
    uint16_t num_queues = 1;
    uint16_t port_id;
    RTE_ETH_FOREACH_DEV(port_id) {
        if (port_config.num_ports >= RTE_MAX_ETHPORTS) {
            fprintf(stderr, "Too many ports\n");
            break;
        }
        if (configure_port(port_id, num_queues)) {
            fprintf(stderr, "Failed to configure port %u\n", port_id);
            continue;
        }
        port_config.port_ids[port_config.num_ports++] = port_id;
        port_config.num_queues = num_queues;
    }

    if (port_config.num_ports == 0) {
        fprintf(stderr, "No available ports\n");
        dpdk_cleanup();
        return -1;
    }

    return 0;
}

void dpdk_cleanup(void) {
    for (int i = 0; i < port_config.num_ports; i++) {
        uint16_t port_id = port_config.port_ids[i];
        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
    }
    if (mbuf_pool) {
        rte_mempool_free(mbuf_pool);
        mbuf_pool = NULL;
    }
}

const dpdk_port_config* dpdk_get_port_config(void) {
    return &port_config;
}

struct rte_mempool* dpdk_get_mempool(void) {
    return mbuf_pool;
}