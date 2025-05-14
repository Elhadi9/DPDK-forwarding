#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include "dpdk.h"

static struct rte_mempool *mbuf_pool = NULL;
static dpdk_port_config port_config = {0};

static int configure_port(uint16_t port) { 
    struct rte_eth_conf port_conf = {0};
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;
    int ret;

    if (!rte_eth_dev_is_valid_port(port)) {
        fprintf(stderr, "Invalid port ID %u\n", port);
        return -EINVAL;
    }

    if ((ret = rte_eth_dev_info_get(port, &dev_info))) {
        fprintf(stderr, "Failed to get device info for port %u: %s\n",
                port, strerror(-ret));
        return ret;
    }

    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    if ((ret = rte_eth_dev_configure(port, 1, 1, &port_conf))) {
        fprintf(stderr, "Failed to configure port %u: %s\n", 
                port, strerror(-ret));
        return ret;
    }

    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    if ((ret = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd))) {
        fprintf(stderr, "Failed to adjust descriptors for port %u\n", port);
        return ret;
    }

    /* Configure RX queue */
    if ((ret = rte_eth_rx_queue_setup(port, 0, nb_rxd,
            rte_eth_dev_socket_id(port), NULL, mbuf_pool))) {
        fprintf(stderr, "RX queue setup failed for port %u: %s\n",
                port, strerror(-ret));
        return ret;
    }

    /* Configure TX queue */
    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    if ((ret = rte_eth_tx_queue_setup(port, 0, nb_txd,
            rte_eth_dev_socket_id(port), &txconf))) {
        fprintf(stderr, "TX queue setup failed for port %u: %s\n",
                port, strerror(-ret));
        return ret;
    }

    if ((ret = rte_eth_dev_start(port))) {
        fprintf(stderr, "Failed to start port %u: %s\n",
                port, strerror(-ret));
        return ret;
    }

    /* Wait for link up */
    struct rte_eth_link link;
    int retry = 0;
    int max_retries = 10;
    
    printf("Waiting for port %u link to come up...\n", port);
    
    do {
        memset(&link, 0, sizeof(link));
        ret = rte_eth_link_get_nowait(port, &link);
        
        if (ret == 0 && link.link_status == RTE_ETH_LINK_UP) {
            printf("Port %u Link Up - Speed %u Mbps - %s\n",
                port, link.link_speed,
                link.link_duplex ? "Full-duplex" : "Half-duplex");
            break;
        }
        
        if (retry++ >= max_retries) {
            fprintf(stderr, "Warning: Port %u link did not come up after %d attempts\n", 
                    port, max_retries);
            fprintf(stderr, "Continuing anyway - port may not be connected\n");
            break;
        }
        
        printf("Waiting for port %u link (attempt %d/%d)...\n", 
               port, retry, max_retries);
        rte_delay_ms(1000);
    } while (retry <= max_retries);

    /* Enable promiscuous mode */
    if ((ret = rte_eth_promiscuous_enable(port)) != 0) {
        fprintf(stderr, "Warning: Promiscuous mode enable failed for port %u: %s\n", 
                port, strerror(-ret));
    }
    
    return 0;
}

void print_port_stats(uint16_t port_id) {
    struct rte_eth_stats stats;
    struct rte_eth_link link;
    
    if (rte_eth_stats_get(port_id, &stats) == 0) {
        printf("Port %u Stats: RX %"PRIu64" pkts (%"PRIu64" err) | TX %"PRIu64" pkts (%"PRIu64" err)\n",
              port_id, 
              stats.ipackets, stats.ierrors,
              stats.opackets, stats.oerrors);
    }
    
    if (rte_eth_link_get_nowait(port_id, &link) == 0) {
        printf("Port %u Link: %s %u Mbps %s\n",
              port_id,
              link.link_status ? "UP" : "DOWN",
              link.link_speed,
              link.link_duplex ? "Full" : "Half");
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
        fprintf(stderr, "Cannot create mbuf pool\n");
        return -1;
    }

    // Initialize ports
    uint16_t port_id;
    RTE_ETH_FOREACH_DEV(port_id) {
        if (port_config.num_ports >= RTE_MAX_ETHPORTS) {
            fprintf(stderr, "Too many ports\n");
            break;
        }
        
        if (configure_port(port_id)) {
            fprintf(stderr, "Failed to configure port %u\n", port_id);
            continue;
        }
        
        port_config.port_ids[port_config.num_ports++] = port_id;
    }

    if (port_config.num_ports == 0) {
        fprintf(stderr, "No available ports\n");
        return -1;
    }
    print_port_stats(port_id);

    return 0;
}

void dpdk_cleanup(void) {
    // Stop and close ports
    for (int i = 0; i < port_config.num_ports; i++) {
        uint16_t port_id = port_config.port_ids[i];
        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
    }
    
    // Free memory pool
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