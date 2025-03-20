#ifndef PRIVATE_H
#define PRIVATE_H

#define BUFFER_SIZE 100
#define MAX_IP_STRING_LENGTH 100


#define TAI_ECGI    130
#define MCC 602
#define MNC 02

struct readConf
{
    int use_dpdk;
    int use_radius_server;
    char *redis_host;
    int redis_port;
    char *radius_ip;
    int radius_port;
    char *radius_server_password;
    char *syslog_dest_ip;
    int syslog_dest_port;
    char *input_mapping_file;
    int using_dpdk;
    int using_radius_server;
    int using_syslog;
    int using_radius;
    char *radius_engine;
    int argc;
    char **argv;
    char *RADIUS_THREAD;
    char *SYSLOG_THREAD;

    int diameter_src_port;
    int diameter_dst_port;
};

#endif