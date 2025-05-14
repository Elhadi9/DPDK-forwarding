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

/* Constants */
#define TEMP_BUF_SIZE        6
#define MAC_ADDR_STR_LEN     18
#define STATS_PRINT_INTERVAL 1000000  // 1 second in microseconds
#define MAX_PORT_NAME_LEN    32

/* Global Context */
static DpdkContext g_ctx = {0};

/* Signal handler for cleanup */
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nShutting down gracefully...\n");
        g_ctx.running = false;
    }
}

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
    }
}

static int parse_portmask(const char *portmask, PortInfo *ports) {
    char *end = NULL;
    unsigned long pm;

    if (!portmask || !*portmask) {
        fprintf(stderr, "Empty portmask\n");
        return -EINVAL;
    }

    errno = 0;
    pm = strtoul(portmask, &end, 16);
    
    if (errno != 0 || end == NULL || *end != '\0') {
        fprintf(stderr, "Invalid portmask format: %s (Must be hexadecimal, e.g., 0x1)\n", portmask);
        return -EINVAL;
    }

    uint16_t port_id;
    unsigned found = 0;
    RTE_ETH_FOREACH_DEV(port_id) {
        if (pm & (1UL << port_id)) {
            if (ports->num_ports >= RTE_MAX_ETHPORTS) {
                fprintf(stderr, "Maximum number of ports (%d) exceeded\n", RTE_MAX_ETHPORTS);
                return -ENOSPC;
            }
            ports->id[ports->num_ports++] = port_id;
            found++;
        }
    }

    if (!found) {
        fprintf(stderr, "No valid ports found in mask 0x%lx\n", pm);
        return -ENODEV;
    }

    return 0;
}

static int configure_port(uint16_t port, const struct rte_mempool *mbuf_pool) {
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

static void cleanup_dpdk(DpdkContext *ctx) {
    if (!ctx) return;

    /* Stop and close ports */
    for (int i = 0; i < ctx->ports.num_ports; i++) {
        uint16_t port = ctx->ports.id[i];
        if (rte_eth_dev_is_valid_port(port)) {
            printf("Stopping port %u\n", port);
            
            struct rte_eth_link link;
            if (rte_eth_link_get_nowait(port, &link) == 0) {
                if (rte_eth_dev_socket_id(port) != SOCKET_ID_ANY) {
                    rte_eth_dev_stop(port);
                    rte_eth_dev_close(port);
                    printf("Port %u closed successfully\n", port);
                }
            }
        }
    }

    /* Free memory pool */
    if (ctx->mbuf_pool) {
        printf("Freeing mbuf pool\n");
        rte_mempool_free(ctx->mbuf_pool);
        ctx->mbuf_pool = NULL;
    }

    printf("DPDK cleanup complete\n");
}

/* Worker thread function for packet processing */
static int worker_main(void *arg) {
    DpdkContext *ctx = (DpdkContext *)arg;
    const unsigned lcore_id = rte_lcore_id();
    const uint64_t timer_freq = rte_get_timer_hz();
    uint64_t prev_tsc = 0;

    printf("Worker thread started on lcore %u\n", lcore_id);

    while (ctx->running) {
        const uint64_t curr_tsc = rte_rdtsc();
        
        /* Print statistics every second */
        if (curr_tsc - prev_tsc > timer_freq) {
            for (int i = 0; i < ctx->ports.num_ports; i++) {
                print_packet_stats(ctx->ports.id[i]);
            }
            prev_tsc = curr_tsc;
        }

        /* Process packets on all ports */
        for (int i = 0; i < ctx->ports.num_ports; i++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t port = ctx->ports.id[i];
            
            if (!rte_eth_dev_is_valid_port(port)) {
                continue;
            }
            
            const uint16_t rx_cnt = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
            if (rx_cnt == 0) continue;

            uint16_t tx_port = port;
            for (int p = 1; p < ctx->ports.num_ports; p++) {
                uint16_t candidate = ctx->ports.id[(i+p) % ctx->ports.num_ports];
                if (rte_eth_dev_is_valid_port(candidate)) {
                    tx_port = candidate;
                    break;
                }
            }

            for (uint16_t j = 0; j < rx_cnt; j++) {
                handle_packet(bufs[j]);
                
                if (port != tx_port) {
                    const uint16_t tx_cnt = rte_eth_tx_burst(tx_port, 0, &bufs[j], 1);
                    if (tx_cnt == 0) {
                        rte_pktmbuf_free(bufs[j]);
                    }
                } else {
                    rte_pktmbuf_free(bufs[j]);
                }
            }
        }
    }

    printf("Worker thread on lcore %u exiting\n", lcore_id);
    return 0;
}

int dpdkInit(int argc, char *argv[]) {
    int ret;

    /* Initialize EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "EAL initialization failed: %s\n", rte_strerror(rte_errno));
        return -1;
    }

    /* Parse application arguments after '--' */
    int app_argc = argc - ret;
    char **app_argv = argv + ret;

    if (app_argc < 2) {
        fprintf(stderr, "Insufficient application arguments\n");
        return -1;
    }

    /* Extract portmask */
    char *portmask = NULL;
    for (int i = 0; i < app_argc; i++) {
        if (strcmp(app_argv[i], "-p") == 0) {
            if (i + 1 < app_argc) {
                portmask = app_argv[i + 1];
                break;
            }
        }
    }

    if (!portmask) {
        fprintf(stderr, "Portmask (-p) not found in arguments\n");
        return -1;
    }

    if (strlen(portmask) < 3 || strncmp(portmask, "0x", 2) != 0) {
        fprintf(stderr, "Invalid portmask format: %s\n", portmask);
        fprintf(stderr, "Portmask must be in hexadecimal format with 0x prefix (e.g., 0x1)\n");
        return -1;
    }

    if (parse_portmask(portmask, &g_ctx.ports) != 0) {
        fprintf(stderr, "Portmask parsing failed\n");
        return -1;
    }

    printf("Detected %d ports in portmask %s\n", g_ctx.ports.num_ports, portmask);
    for (int i = 0; i < g_ctx.ports.num_ports; i++) {
        printf("Using port ID: %u\n", g_ctx.ports.id[i]);
    }

    /* Create memory pool */
    g_ctx.mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 
        NUM_MBUFS * g_ctx.ports.num_ports,
        MBUF_CACHE_SIZE, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id());

    if (!g_ctx.mbuf_pool) {
        fprintf(stderr, "MBUF pool creation failed: %s\n", rte_strerror(rte_errno));
        return -1;
    }

    /* Configure ports */
    bool any_port_configured = false;
    for (int i = 0; i < g_ctx.ports.num_ports; i++) {
        ret = configure_port(g_ctx.ports.id[i], g_ctx.mbuf_pool);
        if (ret == 0) {
            any_port_configured = true;
        } else {
            fprintf(stderr, "Warning: Port %u configuration failed\n", g_ctx.ports.id[i]);
        }
    }

    if (!any_port_configured) {
        fprintf(stderr, "Error: No ports could be configured\n");
        cleanup_dpdk(&g_ctx);
        return -1;
    }

    /* Register signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Launch worker threads on available lcores */
    g_ctx.running = true;
    g_ctx.num_workers = 0;
    
    unsigned lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (g_ctx.num_workers >= MAX_WORKER_LCORES) {
            printf("Warning: Reached maximum number of worker lcores (%d)\n", MAX_WORKER_LCORES);
            break;
        }
        
        g_ctx.worker_lcore_ids[g_ctx.num_workers++] = lcore_id;
        rte_eal_remote_launch(worker_main, &g_ctx, lcore_id);
        printf("Launched worker thread on lcore %u\n", lcore_id);
    }

    if (g_ctx.num_workers == 0) {
        printf("Warning: No worker lcores available, running on main lcore\n");
        worker_main(&g_ctx);
    } else {
        /* Main lcore waits for workers to complete */
        RTE_LCORE_FOREACH_WORKER(lcore_id) {
            if (rte_eal_wait_lcore(lcore_id) < 0) {
                fprintf(stderr, "Worker lcore %u exited with error\n", lcore_id);
            }
        }
    }

    cleanup_dpdk(&g_ctx);
    return 0;
}