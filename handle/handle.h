#ifndef HANDLE_H
#define HANDLE_H

#include <rte_mbuf.h>

#define ETH_P_8021Q     0x8100 /* 802.1Q VLAN Extended Header  */
#define ETH_P_8021AD    0x88A8 /* 802.1ad Service VLAN     */
#define ETH_P_ARP	0x0806 
#define ETH_P_IPV4	0x0800
#define ETH_P_IPV6	0x86DD

/* Configuration Constants */
#define MAX_IP_STRING_LEN    16
#define ETH_VLAN_HLEN        4

struct packet_data
{
    char *in;
    int in_len;
    int in_pos;
};

void handle_packet(struct rte_mbuf *buf);
void packet_handler_set_dst_mac(const uint8_t mac[6]);

#endif
