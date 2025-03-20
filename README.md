Simple DPDK-Based Packet Forwarder

Description
A lightweight, high-performance packet forwarding application built using DPDK. It reads all required arguments from a JSON configuration file.

Compilation and Execution Instructions
1. Compile the Program
Run the following command to compile the program:
make

3. Run the Program
./build/libradius-linux-shared

Dependencies
The following dependencies are required to build and run the project:

GCC or Clang compiler
Make
DPDK
Json-C (https://github.com/json-c/json-c/0)

Expected Output
When the program runs successfully, you should see output similar to:
SRC IP: 192.168.1.1
DST IP: 10.0.0.1
Additionally, the program will process network traffic and display port statistics.
