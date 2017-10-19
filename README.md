---
## RDMA-CS v1

A basic single client/single server model that lacks the full feature set.

---
## RDMA-CS v2

A multithreaded client/server that allows for multiple client conenction to one server. Basic administration
features are added alongside the increased functionality.

---
## RDMA kernel module

This kernel module is a very messy test. I was trying to learn to how make kernel modules and interact with the drivers. The main accomplishment to come out of that is my comprehension of how the fast-reg memory system works. It was built on centos 6.8 with Mellanox OFED 3.3 drivers and kernel version 3.10.87. To build it, you must copy the header files ib_verbs.h and rdma_cm.h from /usr/src/ofa_kernel/default/include/rdma to your kernel's build directory (in my case /lib/modules/3.10.87/build/include/rdma/). You also need to change the ip in the init function to the one that your server will have. From there, on two seperate machines, build the module with make. Then, on the server, run 'make server'. Finally, on the client, run 'make client'. Both sides should return -1, but this should be normal. Check the system log with dmesg to see if it ran correctly or ran into an error.
