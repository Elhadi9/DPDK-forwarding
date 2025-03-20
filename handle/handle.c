#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include "handle.h"
#include "../private.h"
#include <pthread.h>
#include "../private.h"
#include <rte_sctp.h>
#include <rte_ether.h>
#include <linux/if_ether.h>

// Global variables
char ip_string_dst[MAX_IP_STRING_LENGTH];
char ip_string_src[MAX_IP_STRING_LENGTH];
extern struct readConf read;
int proto;
uint16_t port_dst, port_src;

#define ETH_P_IPV4 0x0800
#define ETH_VLAN_HLEN 4

void get_ip_string(const long *ip, char *buf)
{
	const u_int8_t *a = (const u_int8_t *)ip;
	sprintf(buf, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

/**
 * @brief Handle all received packets by DPDK.
 *
 * @param buf The received packet buffer.
 */
void handle_packet(struct rte_mbuf *buf)
{
    uint32_t ip_dst, ip_src;
    uint16_t sctp_offset = 0;
    struct linux_capture *capt;
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    struct rte_udp_hdr *udp;
    struct rte_tcp_hdr *tcp;
    struct rte_sctp_hdr *rte_sctp_hdr = NULL;

    // Extract Ethernet header
    eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

    // Check for 802.1Q VLAN header (0x8100 indicates VLAN tagging)
    if (rte_be_to_cpu_16(eth_hdr->ether_type) == ETH_P_8021Q)
    {
        // Skip the VLAN header (4 bytes)
        eth_hdr = (struct rte_ether_hdr *)((unsigned char *)eth_hdr + ETH_VLAN_HLEN);
    }

    // Drop non-IPv4 packets
    if (rte_be_to_cpu_16(eth_hdr->ether_type) != ETH_P_IPV4) {  
        rte_pktmbuf_free(buf);  // Free the packet buffer
        return;                 // Ignore non-IPv4 packets
    }
    
    // Modify the destination MAC address
    struct rte_ether_addr new_dst_mac = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}}; // random MAC
    rte_ether_addr_copy(&new_dst_mac, &eth_hdr->dst_addr);

    if (eth_hdr->ether_type != 0)
        ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    else
    {
        capt = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
        ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    }

    // Check if ipv4_hdr is NULL
    if (!ipv4_hdr)
    {
        rte_pktmbuf_free(buf);
        return;
    }

    // Verify packet length
    uint16_t temp_len = rte_pktmbuf_pkt_len(buf);
    if (sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) > temp_len)
    {
        return;
    }

    if (ipv4_hdr->total_length < temp_len)
    {
        temp_len = ipv4_hdr->total_length;
    }

    proto = ipv4_hdr->next_proto_id;
    temp_len -= sizeof(struct rte_ipv4_hdr);

    switch (ipv4_hdr->next_proto_id)
    {
    case IPPROTO_TCP:
        tcp = (struct rte_tcp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        port_dst = rte_be_to_cpu_16(tcp->dst_port);
        port_src = rte_be_to_cpu_16(tcp->src_port);
        break;
    case IPPROTO_UDP:
        udp = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        port_dst = rte_be_to_cpu_16(udp->dst_port);
        port_src = rte_be_to_cpu_16(udp->src_port);
        if (udp->dgram_len > temp_len)
        {
            udp->dgram_len = temp_len;
        }
        break;
    default:
        port_dst = 0;
        port_src = 0;
        return;
    }

    // Convert IP addresses to strings
    get_ip_string(&(ipv4_hdr->dst_addr), ip_string_dst);
    get_ip_string(&(ipv4_hdr->src_addr), ip_string_src);

    printf("\nSRC IP: %s\n", ip_string_src);
    printf("DST IP: %s\n", ip_string_dst);

}