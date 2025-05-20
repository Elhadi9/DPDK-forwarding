#ifndef PRIVATE_H
#define PRIVATE_H

#define BUFFER_SIZE 100
#define MAX_IP_STRING_LENGTH 100


#define TAI_ECGI    130
#define MCC 602
#define MNC 02

#define MAX_WORKER_CORES 8

struct readConf
{
    int argc;
    char **argv;
};

// Worker core configuration
struct worker_config {
    unsigned lcore_id;
    uint16_t queue_id; // Queue assigned to this core
    uint16_t port_id;  // Port assigned to this core
};

#endif
