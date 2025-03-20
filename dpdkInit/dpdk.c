#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include "dpdk.h"
#include "../handle/handle.h"
#include "../private.h"

// Global variables
struct port_info ports;
extern struct readConf read;

void print_packet_stats(uint16_t port_id) {
    struct rte_eth_stats stats;
    if (rte_eth_stats_get(port_id, &stats) == 0) {
        printf("Port %u Stats:\n", port_id);
        printf("  RX Packets: %" PRIu64 "\n", stats.ipackets);
        printf("  TX Packets: %" PRIu64 "\n", stats.opackets);
        printf("  RX Bytes  : %" PRIu64 "\n", stats.ibytes);
        printf("  TX Bytes  : %" PRIu64 "\n", stats.obytes);
        printf("  RX Errors : %" PRIu64 "\n", stats.ierrors);
        printf("  TX Errors : %" PRIu64 "\n", stats.oerrors);
    } else {
        printf("Failed to get stats for port %u\n", port_id);
    }
}

/**
 * @brief Parse port mask and identify ports to be used.
 *
 * @param portmask The port mask as a string.
 * @return 0 on success, -1 on failure.
 */
int parse_portmask(const char *portmask)
{
    char *end = NULL;
    unsigned long long pm;
    uint16_t id;

    if (portmask == NULL || *portmask == '\0')
        return -1;

    // Convert parameter to a number and verify
    errno = 0;
    pm = strtoull(portmask, &end, 16);
    if (errno != 0 || end == NULL || *end != '\0')
        return -1;

    // Iterate over each port and add valid ports to the ports struct
    RTE_ETH_FOREACH_DEV(id)
    {
        unsigned long msk = 1u << id;

        if ((pm & msk) == 0)
            continue;

        pm &= ~msk;
        ports.id[ports.num_ports++] = id;
    }

    if (pm != 0)
    {
        printf("WARNING: leftover ports in mask %#llx - ignoring\n", pm);
    }
    return 0;
}

/**
 * @brief Initialize a port with given configurations.
 *
 * @param port The port to initialize.
 * @param mbuf_pool The memory buffer pool for packet storage.
 * @return 0 on success, negative on failure.
 */
int port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}

/**
 * @brief Main processing loop for handling packets on the lcore.
 */
__rte_noreturn void lcore_main(void)
{
    uint16_t port;

    // Check NUMA alignment and warn if ports are on remote NUMA node
    for (int i = 0; i < ports.num_ports; i++)
    {
        if (rte_eth_dev_socket_id(ports.id[i]) >= 0 && rte_eth_dev_socket_id(ports.id[i]) != (int)rte_socket_id())
            printf("WARNING, port %u is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", ports.id[i]);
    }

    printf("\n[Ctrl+C to quit]\n");

    // Infinite loop for processing packets
    for (;;)
    {
        for (int i = 0; i < ports.num_ports; i++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(ports.id[i], 0, bufs, BURST_SIZE);
        
            if (nb_rx == 0)
                continue;
        
            for (unsigned j = 0; j < nb_rx; j++) {
                uint16_t src_port = ports.id[i];
                uint16_t dst_port = ports.id[(i + 1) % ports.num_ports];  // Forward to the next port
        
                handle_packet(bufs[j]);
        
                // Transmit packets to the next port
                const uint16_t nb_tx = rte_eth_tx_burst(dst_port, 0, &bufs[j], 1);
        
                if (nb_tx < 1) {
                    rte_pktmbuf_free(bufs[j]); // Drop packet if transmission fails
                }
            }
        
            print_packet_stats(ports.id[i]);
        }
    }
}

/**
 * @brief Initialize the DPDK environment and launch worker threads.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 */
void dpdkInit(int argc, char *argv[])
{
    int retval;
    uint16_t i;
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    // Parse the port mask from the arguments
    char temp[6] = {0};
    sprintf(temp, "%s", &argv[1][2]);
    parse_portmask(temp);
    nb_ports = ports.num_ports;

    // Create memory buffer pool
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    // Initialize each port
    for (i = 0; i < ports.num_ports; i++)
    {
        retval = port_init(ports.id[i], mbuf_pool);
        if (retval != 0)
            rte_exit(EXIT_FAILURE, "Cannot initialise port %u\n", (unsigned)i);
    }

    // Enter main processing loop
    lcore_main();
    rte_eal_cleanup();
}
