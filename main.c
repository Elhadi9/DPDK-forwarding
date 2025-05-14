#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include "dpdk/dpdk.h"
#include "packet_processor/processor.h"

#define MAX_WORKER_CORES 8

static volatile bool running = true;

// Enhanced signal handler
void signal_handler(int signum)
{
    printf("\nReceived signal %d\n", signum);
    running = false;

    // // Notify worker cores
    // for (int i = 0; i < num_workers; i++)
    // {
    //     rte_eal_mp_remote_launch(stop_workers, NULL, CALL_MASTER);
    // }
}

int main(int argc, char *argv[])
{
    int ret;
    unsigned lcore_id;
    unsigned worker_lcores[MAX_WORKER_CORES] = {0};
    unsigned num_workers = 0;

    // Parse EAL arguments
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        fprintf(stderr, "EAL initialization failed\n");
        return EXIT_FAILURE;
    }
    printf("DPDK initialized successfully\n");
    printf("Configured ports:\n");
    for (int i = 0; i < dpdk_get_port_config()->num_ports; i++)
    {
        printf("  Port %u\n", dpdk_get_port_config()->port_ids[i]);
    }

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize DPDK
    if (dpdk_initialize() != 0)
    {
        fprintf(stderr, "DPDK initialization failed\n");
        return EXIT_FAILURE;
    }

    // Find available worker cores
    RTE_LCORE_FOREACH_WORKER(lcore_id)
    {
        if (num_workers < MAX_WORKER_CORES)
        {
            worker_lcores[num_workers++] = lcore_id;
            printf("Assigned worker core: %u\n", lcore_id); // Debugging line
        }
    }

    if (num_workers == 0)
    {
        fprintf(stderr, "No worker cores available\n");
        dpdk_cleanup();
        return EXIT_FAILURE;
    }

    printf("Using %d worker cores for packet processing\n", num_workers);

    // Launch packet processors on worker cores
    for (int i = 0; i < num_workers; i++)
    {
        ret = rte_eal_remote_launch(packet_processor_main, NULL, worker_lcores[i]);
        if (ret != 0)
        {
            printf("Failed to launch worker core %u\n", worker_lcores[i]);
        }
        else
        {
            printf("Launched worker core %u\n", worker_lcores[i]);
        }
    }

    // Main loop
    uint64_t last_stats_print = 0;
    const uint64_t stats_interval = rte_get_tsc_hz(); // 1 second

    while (running)
    {
        uint64_t now = rte_rdtsc();

        // Print status every second instead of spamming
        if (now - last_stats_print > stats_interval)
        {
            printf("Main core active - Workers running\n");
            last_stats_print = now;

            // Optional: Print port statistics
            for (int i = 0; i < dpdk_get_port_config()->num_ports; i++)
            {
                print_port_stats(dpdk_get_port_config()->port_ids[i]);
            }
        }

        rte_delay_us(1000); // More reasonable delay
    }

    // Wait for workers to complete
    for (int i = 0; i < num_workers; i++)
    {
        rte_eal_wait_lcore(worker_lcores[i]);
        printf("Worker %u completed\n", worker_lcores[i]); // Debugging line
    }

    // Cleanup
    dpdk_cleanup();
    return EXIT_SUCCESS;
}