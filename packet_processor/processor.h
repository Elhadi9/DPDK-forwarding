#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <rte_mbuf.h>

#define ETH_P_8021Q     0x8100 /* 802.1Q VLAN Extended Header  */
#define ETH_P_8021AD    0x88A8 /* 802.1ad Service VLAN     */
#define ETH_P_ARP	0x0806 
#define ETH_P_IPV4	0x0800
#define ETH_P_IPV6	0x86DD

#define BURST_TX_DRAIN_US 100

/* Configuration Constants */
#define MAX_IP_STRING_LEN    16
#define ETH_VLAN_HLEN        4

int packet_processor_main(void *arg);

#endif // PROCESSOR_H