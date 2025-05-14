#include <stdio.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include "processor.h"
#include "../dpdk/dpdk.h"
#include "../private.h"

static volatile bool running = true;


/* Thread-local packet counter */
RTE_DEFINE_PER_LCORE(uint64_t, packet_count) = 0;

/* Packet Metadata Structure */
typedef struct {
    char src_ip[MAX_IP_STRING_LEN];
    char dst_ip[MAX_IP_STRING_LEN];
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
} PacketMetadata;

/* Helper Functions */
static inline bool validate_packet_length(struct rte_mbuf *buf, size_t required) {
    return rte_pktmbuf_data_len(buf) >= required;
}

static void format_ipv4_address(uint32_t addr, char *buffer) {
    const uint8_t *bytes = (const uint8_t *)&addr;
    snprintf(buffer, MAX_IP_STRING_LEN, "%u.%u.%u.%u", 
             bytes[0], bytes[1], bytes[2], bytes[3]);
}

/* Protocol Handlers */
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
    if (!validate_packet_length((struct rte_mbuf *)eth_hdr, 
                               sizeof(*eth_hdr) + sizeof(struct rte_ipv4_hdr))) {
        return false;
    }

    struct rte_ipv4_hdr *ipv4 = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    format_ipv4_address(ipv4->src_addr, meta->src_ip);
    format_ipv4_address(ipv4->dst_addr, meta->dst_ip);

    return handle_transport_layer(ipv4, meta);
}

static struct rte_ether_hdr * handle_vlan(struct rte_ether_hdr *eth_hdr) {
    if (rte_be_to_cpu_16(eth_hdr->ether_type) == ETH_P_8021Q) {
        return (struct rte_ether_hdr *)((uint8_t *)eth_hdr + ETH_VLAN_HLEN);
    }
    return eth_hdr;
}

/* Main Packet Handler */
void handle_packet(struct rte_mbuf *buf) {
    PacketMetadata meta = {0};
    unsigned lcore_id = rte_lcore_id();

    /* Update per-lcore packet counter */
    RTE_PER_LCORE(packet_count)++;

    /* Extract Ethernet header */
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
    
    /* Handle VLAN tagging */
    eth_hdr = handle_vlan(eth_hdr);
    
    /* Process IPv4 packets only */
    if (rte_be_to_cpu_16(eth_hdr->ether_type) != ETH_P_IPV4) {
        rte_pktmbuf_free(buf);
        return;
    }

    /* Process IP and transport layers */
    if (!handle_ipv4(eth_hdr, &meta)) {
        rte_pktmbuf_free(buf);
        return;
    }

    /* Log packet information (thread-safe) */
    printf("[Core %u] Packet %"PRIu64":\n"
           "  SRC: %s:%"PRIu16"\n"
           "  DST: %s:%"PRIu16"\n"
           "  Protocol: %s\n",
           lcore_id, RTE_PER_LCORE(packet_count),
           meta.src_ip, meta.src_port,
           meta.dst_ip, meta.dst_port,
           meta.protocol == IPPROTO_TCP ? "TCP" : "UDP");

    /* Forward packet or other processing */
    /* ... */
}

int packet_processor_main(void *arg) {
    const dpdk_port_config *config = dpdk_get_port_config();
    const unsigned lcore_id = rte_lcore_id();
    uint64_t total_packets = 0;
    uint64_t last_print = 0;
    const uint64_t print_interval = rte_get_tsc_hz(); // 1 second
    
    printf("Packet processor started on core %u\n", lcore_id);
    
    while (running) {
        bool work_done = false;
        uint64_t start_cycle = rte_rdtsc();
        
        // Process all ports
        for (int i = 0; i < config->num_ports; i++) {
            uint16_t port_id = config->port_ids[i];
            struct rte_mbuf *mbufs[BURST_SIZE];
            uint16_t nb_rx;
            
            nb_rx = rte_eth_rx_burst(port_id, 0, mbufs, BURST_SIZE);
            if (nb_rx == 0)
                continue;
                
            work_done = true;
            total_packets += nb_rx;
            
            for (int j = 0; j < nb_rx; j++) {
                handle_packet(mbufs[j]);
                rte_pktmbuf_free(mbufs[j]);
            }
        }
        
        // Print throughput statistics periodically
        if (start_cycle - last_print > print_interval) {
            printf("Core %u processed %"PRIu64" packets (%.2f Mpps)\n",
                  lcore_id, total_packets, 
                  (double)total_packets / 1000000);
            total_packets = 0;
            last_print = start_cycle;
        }
        
        if (!work_done) {
            rte_pause(); // More efficient than delay
        }
    }
    
    printf("Packet processor exiting on core %u\n", lcore_id);
    return 0;
}