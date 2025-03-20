#ifndef HANDLE_H
#define HANDLE_H

#include <rte_mbuf.h>

struct packet_data
{
    char *in;
    int in_len;
    int in_pos;
};

void handle_packet(struct rte_mbuf *buf);
void get_ip_string(const long *ip, char *buf);

#endif
