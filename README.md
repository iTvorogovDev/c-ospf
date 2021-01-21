# c-ospf
A C/C++ command line-based program implementing OSPF routing algorithm in a network of five routers I wrote as part of my Computer Networks course.

# DISCLAIMER
The code provided in this repository is meant **ONLY** as a portfolio entry for the potential employer's consideration. All binary files provided here are the intellectual property of University of Waterloo. Reusing any code provided here in your own assignments is an academic offense.

# TECHNICAL SPECIFICATIONS
1. Built and tested in Bash environment
2. C/C++ code compiled with g++ (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0
3. Built with GNU Make 4.1

# INSTRUCTIONS
1. From within the root folder, run make command
1. Start up the network emulator (see specifications below). It will emulate a hardcoded network of five routers.
1. Start up five separate instances of router program (see specifications below). The <nse_host> parameter can be in IPv4 or a hostname format.

# PROGRAM EXECUTION FORMAT
nse \<routers_host\> \<nse_port\>  
where
* \<routers_host\> is the host where the routers are running. For the assignment purpose, it is assumed that all
routers are running on the **same** host.
* \<nse_port\> is the Network State Emulator port number.

router \<router_id\> \<nse_host\> \<nse_port> \<router_port\>  
where
* \<router_id\> is an integer that represents the router id. It should be unique for each router.
* \<nse_host\> is the host where the Network State Emulator is running.
* \<nse_port\> is the port number of the Network State Emulator.
* \<router_port\> is the router port

* On **hostX**
nse hostY 9999
* On **hostY**
  * router 1 hostX 9999 9991
  * router 2 hostX 9999 9992
  * router 3 hostX 9999 9993
  * router 4 hostX 9999 9994
  * router 5 hostX 9999 9995
* Expected output
  * router1.log router2.log router3.log router4.log router5.log
