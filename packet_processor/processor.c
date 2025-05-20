#include <stdio.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_atomic.h>
#include "processor.h"
#include "../dpdk/dpdk.h"
#include "../private.h"

static rte_atomic32_t keep_running = RTE_ATOMIC32_INIT(1);

// Per-core packet counters
RTE_DEFINE_PER_LCORE(uint64_t, packet_count) = 0;
RTE_DEFINE_PER_LCORE(uint64_t, udp_forwarded) = 0;
RTE_DEFINE_PER_LCORE(uint64_t, tcp_dropped) = 0;

// Packet metadata structure
typedef struct {
    char src_ip[MAX_IP_STRING_LEN];
    char dst_ip[MAX_IP_STRING_LEN];
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
} PacketMetadata;

// Helper functions
static inline bool validate_packet_length(struct rte_mbuf *buf, size_t required) {
    return rte_pktmbuf_data_len(buf) >= required;
}

static void format_ipv4_address(uint32_t addr, char *buffer) {
    const uint8_t *bytes = (const uint8_t *)&addr;
    snprintf(buffer, MAX_IP_STRING_LEN, "%u.%u.%u.%u",
             bytes[0], bytes[1], bytes[2], bytes[3]);
}

// Protocol handlers
static bool handle_transport_layer(struct rte_ipv4_hdr *ipv4, PacketMetadata *meta) {
    void *transport_hdr = (uint8_t *)ipv4 + sizeof(*ipv4);
    uint16_t payload_len = rte_be_to_cpu_16(ipv4->total_length) - sizeof(*ipv4);

    switch (ipv4->next_proto_id) {
        case IPPROTO_TCP: {
            if (payload_len < sizeof(struct rte_tcp_hdr)) return false;
            struct rte_tcp_hdr *tcp = transport_hdr;
            meta->src_port = rte_be_to_cpu_16(tcp->src_port);
            meta->dst_port = rte_be_to_cpu_16(tcp->dst_port);
            break;
        }
        case IPPROTO_UDP: {
            if (payload_len < sizeof(struct rte_udp_hdr)) return false;
            struct rte_udp_hdr *udp = transport_hdr;
            meta->src_port = rte_be_to_cpu_16(udp->src_port);
            meta->dst_port = rte_be_to_cpu_16(udp->dst_port);
            break;
        }
        default:
            return false;
    }
    meta->protocol = ipv4->next_proto_id;
    return true;
}

static bool handle_ipv4(struct rte_ether_hdr *eth_hdr, PacketMetadata *meta) {
    if (!validate_packet_length((struct rte_mbuf *)eth_hdr, sizeof(*eth_hdr) + sizeof(struct rte_ipv4_hdr))) {
        return false;
    }
    struct rte_ipv4_hdr *ipv4 = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    format_ipv4_address(ipv4->src_addr, meta->src_ip);
    format_ipv4_address(ipv4->dst_addr, meta->dst_ip);
    return handle_transport_layer(ipv4, meta);
}

static struct rte_ether_hdr *handle_vlan(struct rte_ether_hdr *eth_hdr) {
    while (rte_be_to_cpu_16(eth_hdr->ether_type) == ETH_P_8021Q) {
        eth_hdr = (struct rte_ether_hdr *)((uint8_t *)eth_hdr + ETH_VLAN_HLEN);
    }
    return eth_hdr;
}

// Main packet handler
static void handle_packet(struct rte_mbuf *buf, uint16_t rx_port_id, uint16_t tx_port_id) {
    PacketMetadata meta = {0};
    RTE_PER_LCORE(packet_count)++;

    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
    eth_hdr = handle_vlan(eth_hdr);

    if (rte_be_to_cpu_16(eth_hdr->ether_type) != ETH_P_IPV4) {
        rte_pktmbuf_free(buf);
        return;
    }

    if (!handle_ipv4(eth_hdr, &meta)) {
        rte_pktmbuf_free(buf);
        return;
    }

    // Check protocol and decide action
    if (meta.protocol == IPPROTO_TCP) {
        RTE_PER_LCORE(tcp_dropped)++;
        // Log every 100th dropped TCP packet
        if (RTE_PER_LCORE(tcp_dropped) % 100 == 0) {
            printf("[Core %u] Dropped TCP: SRC %s:%u -> DST %s:%u\n",
                   rte_lcore_id(), meta.src_ip, meta.src_port, meta.dst_ip, meta.dst_port);
        }
        rte_pktmbuf_free(buf);
        return;
    } else if (meta.protocol == IPPROTO_UDP) {
        // Log every 100th forwarded UDP packet
        if (RTE_PER_LCORE(packet_count) % 100 == 0) {
            printf("[Core %u] Forwarding UDP: SRC %s:%u -> DST %s:%u from Port %u to Port %u\n",
                   rte_lcore_id(), meta.src_ip, meta.src_port, meta.dst_ip, meta.dst_port,
                   rx_port_id, tx_port_id);
        }
        // Check if TX port is up before forwarding
        struct rte_eth_link link;
        rte_eth_link_get_nowait(tx_port_id, &link);
        if (link.link_status != RTE_ETH_LINK_UP) {
            RTE_PER_LCORE(tcp_dropped)++; // Count as dropped
            rte_pktmbuf_free(buf);
            return;
        }
        // Forward packet
        uint16_t nb_tx = rte_eth_tx_burst(tx_port_id, 0, &buf, 1);
        if (nb_tx == 1) {
            RTE_PER_LCORE(udp_forwarded)++;
        } else {
            RTE_PER_LCORE(tcp_dropped)++; // Count as dropped if TX fails
            rte_pktmbuf_free(buf);
        }
    } else {
        rte_pktmbuf_free(buf);
    }
}

int packet_processor_main(void *arg) {
    struct worker_config *config = (struct worker_config *)arg;
    unsigned lcore_id = rte_lcore_id();
    uint16_t rx_port_id = config->port_id;
    uint16_t tx_port_id = config->tx_port_id;
    uint16_t queue_id = config->queue_id;
    uint64_t total_packets = 0;
    uint64_t last_print = 0;
    const uint64_t print_interval = rte_get_tsc_hz(); // 1 second

    printf("Packet processor started on core %u, RX port %u, TX port %u, queue %u\n", 
           lcore_id, rx_port_id, tx_port_id, queue_id);

    while (rte_atomic32_read(&keep_running)) {
        bool work_done = false;
        uint64_t start_cycle = rte_rdtsc();

        // Process packets from assigned RX port and queue
        struct rte_mbuf *mbufs[BURST_SIZE];
        uint16_t nb_rx = rte_eth_rx_burst(rx_port_id, queue_id, mbufs, BURST_SIZE);
        if (nb_rx > 0) {
            work_done = true;
            total_packets += nb_rx;

            for (uint16_t j = 0; j < nb_rx; j++) {
                handle_packet(mbufs[j], rx_port_id, tx_port_id);
            }
        }

        // Periodic stats printing
        if (start_cycle - last_print > print_interval) {
            printf("Core %u, RX Port %u, Queue %u: Processed %"PRIu64" pkts, Forwarded %"PRIu64" UDP, Dropped %"PRIu64" TCP (%.2f Mpps)\n",
                   lcore_id, rx_port_id, queue_id, total_packets, RTE_PER_LCORE(udp_forwarded), 
                   RTE_PER_LCORE(tcp_dropped), (double)total_packets / 1000000);
            total_packets = 0;
            RTE_PER_LCORE(udp_forwarded) = 0;
            RTE_PER_LCORE(tcp_dropped) = 0;
            last_print = start_cycle;
        }

        if (!work_done) {
            rte_pause();
        }
    }

    printf("Packet processor exiting on core %u, RX port %u, TX port %u, queue %u\n", 
           lcore_id, rx_port_id, tx_port_id, queue_id);
    return 0;
}

int stop_packet_processor(void *arg) {
    rte_atomic32_set(&keep_running, 0);
    return 0;
}