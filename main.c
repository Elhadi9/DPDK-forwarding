#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include "dpdk/dpdk.h"
#include "packet_processor/processor.h"
#include "private.h"

// Signal handler context
static volatile int keep_running = 1;

// Signal handler to stop workers cleanly
static void signal_handler(int signum)
{
    printf("\nReceived signal %d, initiating shutdown\n", signum);
    keep_running = 0;
    rte_eal_mp_remote_launch(stop_packet_processor, NULL, SKIP_MAIN);
}

int main(int argc, char *argv[])
{
    int ret;
    unsigned lcore_id;
    struct worker_config workers[MAX_WORKER_CORES] = {0};
    unsigned num_workers = 0;

    // Initialize DPDK EAL on main core
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "EAL initialization failed: %s\n", rte_strerror(ret));
        return EXIT_FAILURE;
    }
    printf("DPDK initialized successfully\n");

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize DPDK (ports, mempool, etc.)
    if (dpdk_initialize() != 0) {
        fprintf(stderr, "DPDK initialization failed\n");
        return EXIT_FAILURE;
    }

    // Print port configuration
    const dpdk_port_config *port_config = dpdk_get_port_config();
    printf("Configured ports:\n");
    for (int i = 0; i < port_config->num_ports; i++) {
        struct rte_eth_link link;
        rte_eth_link_get_nowait(port_config->port_ids[i], &link);
        printf("  Port %u (Link: %s)\n", port_config->port_ids[i], 
               link.link_status ? "UP" : "DOWN");
    }

    // Assign worker cores to active ports (multiple cores per port)
    unsigned port_idx = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (num_workers >= MAX_WORKER_CORES) break;
        while (port_idx < port_config->num_ports) {
            uint16_t rx_port_id = port_config->port_ids[port_idx];
            struct rte_eth_link link;
            rte_eth_link_get_nowait(rx_port_id, &link);
            if (link.link_status == RTE_ETH_LINK_UP) {
                // Assign RX and TX ports (opposite port for forwarding)
                uint16_t tx_port_id = (port_idx + 1) % port_config->num_ports;
                // Ensure TX port is up
                rte_eth_link_get_nowait(port_config->port_ids[tx_port_id], &link);
                if (port_config->num_ports > 1 && link.link_status != RTE_ETH_LINK_UP) {
                    printf("Warning: TX Port %u is down for RX Port %u, packets may be dropped\n", 
                           tx_port_id, rx_port_id);
                }
                workers[num_workers].lcore_id = lcore_id;
                workers[num_workers].port_id = rx_port_id;
                workers[num_workers].tx_port_id = tx_port_id;
                workers[num_workers].queue_id = 0; // Single queue
                num_workers++;
                printf("Assigned worker core %u to RX port %u, TX port %u, queue 0\n", 
                       lcore_id, rx_port_id, tx_port_id);
                break; // Stay on this port for more cores
            } else {
                printf("Skipping RX port %u (link down)\n", rx_port_id);
                port_idx++;
            }
        }
    }

    if (num_workers == 0) {
        fprintf(stderr, "No worker cores available or no active ports\n");
        dpdk_cleanup();
        return EXIT_FAILURE;
    }

    printf("Using %u worker cores for packet processing\n", num_workers);

    // Launch packet processors on worker cores
    for (int i = 0; i < num_workers; i++) {
        ret = rte_eal_remote_launch(packet_processor_main, &workers[i], workers[i].lcore_id);
        if (ret != 0) {
            fprintf(stderr, "Failed to launch worker core %u: %s\n", workers[i].lcore_id, rte_strerror(ret));
        } else {
            printf("Launched worker core %u on RX port %u, TX port %u, queue 0\n", 
                   workers[i].lcore_id, workers[i].port_id, workers[i].tx_port_id);
        }
    }

    // Main core monitoring loop
    uint64_t last_stats_print = 0;
    const uint64_t stats_interval = rte_get_tsc_hz(); // 1 second

    while (keep_running) {
        uint64_t now = rte_rdtsc();
        if (now - last_stats_print > stats_interval) {
            printf("Main core: Monitoring %u workers\n", num_workers);
            for (int i = 0; i < port_config->num_ports; i++) {
                print_port_stats(port_config->port_ids[i]);
            }
            last_stats_print = now;
        }
        rte_pause();
    }

    // Wait for workers to exit
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (rte_eal_get_lcore_state(lcore_id) == RUNNING) {
            rte_eal_wait_lcore(lcore_id);
            printf("Worker core %u stopped\n", lcore_id);
        }
    }

    // Cleanup DPDK resources
    dpdk_cleanup();
    printf("DPDK cleanup completed\n");
    return EXIT_SUCCESS;
}