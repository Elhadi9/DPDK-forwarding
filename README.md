DPDK-Based Packet Processor and Forwarder


Description
This is a high-performance packet processing application built using the Data Plane Development Kit (DPDK). The application captures network packets, processes them, and optionally forwards them based on predefined rules. It is designed to leverage multi-core CPU architectures for efficient packet processing and supports filtering and forwarding capabilities.


Branches

- new_core_structure: Implements a multi-core packet processing framework where multiple CPU cores poll a single queue per active port. This branch is optimized for high-throughput packet processing, utilizing up to 8 worker cores to handle incoming packets concurrently.
- 
- forwarding: Extends the new_core_structure branch by adding packet forwarding capabilities. It forwards UDP packets from one port to another (e.g., Port 0 to Port 1) while dropping TCP packets. This branch includes enhanced statistics for forwarded and dropped packets.


Features

Multi-core packet processing with up to 8 worker cores per active port.

Single-queue configuration for simplicity and stability (no RSS).

UDP packet forwarding between ports (e.g., Port 0 to Port 1).

TCP packet filtering (drops all TCP packets).

VLAN tag handling for Ethernet frames.

Detailed per-core and per-port statistics, including packets processed, forwarded, and dropped.

Promiscuous mode support for capturing all network traffic.

Graceful shutdown with signal handling (SIGINT, SIGTERM).


Dependencies

The following dependencies are required to build and run the project:

DPDK (23.11 or later): Provides the core framework for packet processing. Install from DPDK releases.

GCC or Clang: C compiler for building the application.

Make: Build automation tool.

Linux Kernel Headers: Required for DPDK’s user-space drivers.

libnuma-dev: NUMA support for DPDK memory allocation.

Hugepages: DPDK requires hugepages for memory management.



Optional Dependencies

pktgen-dpdk: For generating test traffic to evaluate performance. Install from DPDK pktgen.



Installation

Install Dependencies:

sudo apt update

sudo apt install -y build-essential libnuma-dev linux-headers-$(uname -r)


Compilation

The project includes a Makefile for easy compilation.

Directory Structure:

.

├── Makefile

├── README.md

├── main.c

├── dpdk/

│   └── dpdk.c

├── packet_processor/

│   └── processor.c

└── private.h



Compile:

make

This generates the executable build/dpdk_app.

Clean Build:

make clean



Execution

Run the application with sudo privileges, as DPDK requires access to hugepages and NICs.
Command

sudo ./build/dpdk_app -l 0-8 -n 4 -- -p 0x1

Arguments

-l 0-8: Use CPU cores 0 (main) and 1–8 (workers).

-n 4: Number of memory channels.

-p 0x1: Port mask to enable Port 0 only (use 0x3 for Ports 0 and 1 if both are up).


Notes

If Port 1 is down, use -p 0x1. UDP packets destined for Port 1 will be dropped with stats.

Ensure NICs are bound to DPDK drivers and hugepages are configured.

Testing

To verify packet processing and forwarding, use pktgen-dpdk to send mixed UDP and TCP traffic.

Install pktgen-dpdk:

git clone https://github.com/pktgen/Pktgen-DPDK.git

cd Pktgen-DPDK

make


Run pktgen:
sudo ./app/pktgen -l 0-3 -n 4 -- -P -m "1:0.0"


Verify Forwarding:

If Port 1 is up, use tcpdump on the receiving device:

sudo tcpdump -i ethX udp

Confirm UDP packets arrive and no TCP packets are forwarded.

Check the application’s output for forwarding and drop stats.




Expected Output

When running the forwarding branch with Port 0 up and Port 1 down:

DPDK initialized successfully

Port 0: max_rx_queues=8, max_tx_queues=8

Configured RX queue 0 for port 0

Port 0 Link Up - Speed 1000 Mbps - Full-duplex

Port 0: Promiscuous mode enabled

Port 1 link is down, skipping configuration

Configured ports:

  Port 0 (Link: UP)
  
Assigned worker core 1 to RX port 0, TX port 1, queue 0

...

Using 8 worker cores for packet processing

Packet processor started on core 1, RX port 0, TX port 1, queue 0

Warning: TX Port 1 is down for RX Port 0, packets may be dropped

[Core 1] Forwarding UDP: SRC 192.168.23.1:1234 -> DST 192.168.23.100:5678 from Port 0 to Port 1

[Core 2] Dropped TCP: SRC 192.168.23.1:1234 -> DST 192.168.23.100:5678

Core 1, RX Port 0, Queue 0: Processed 10000 pkts, Forwarded 0 UDP, Dropped 10000 TCP (1.00 Mpps)

Port 0 Stats: RX 10000 pkts (0 err) | TX 0 pkts (0 err)

If Port 1 is up, expect non-zero TX stats for Port 1 and forwarded UDP packets.

Troubleshooting

No Packets Captured:

Verify NIC binding: sudo dpdk-devbind.py --status.

Ensure promiscuous mode is enabled (check output for Promiscuous mode enabled).

Check network traffic with testpmd:

sudo testpmd -l 0-1 -n 4 -- -i --portmask=0x1

testpmd> set promisc all on

testpmd> start


Port 1 Link Down:

Check physical connection (cable, switch).

Rebind NIC:

sudo dpdk-devbind.py --bind=igb_uio 0000:01:00.3




Segmentation Fault:

Ensure DPDK is installed correctly and hugepages are configured.

Enable verbose logging:

sudo ./build/dpdk_app -l 0-8 -n 4 --log-level=pmd,8 -- -p 0x1





License

Not yet

Contributing

Contributions are welcome! Please submit pull requests to the new_core_structure or forwarding branch, depending on the feature.
