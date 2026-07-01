# **vntun \- Userspace Layer 3 VPN Daemon**

vntun is a lightweight, point-to-point Layer 3 Virtual Private Network (VPN) daemon written in C for Linux.

## **Motivation**

This project was built from first principles to deeply explore the boundary between user space and kernel space. Rather than wrapping existing high-level libraries, vntun was developed to provide hands-on implementation experience with Linux networking internals, including virtual interface manipulation (/dev/net/tun), asynchronous event multiplexing (epoll), raw IPv4 header parsing, and custom UDP packet encapsulation.

## **Features**

* **Linux Virtual Device Integration:** Programmatic creation and management of TUN interfaces using ioctl.  
* **Event-Driven Concurrency:** Single-threaded, non-blocking packet processing loop driven by the Linux epoll API.

* **Raw IP Parsing:** Direct inspection of IPv4 headers to filter packets and strictly control routing targets.  
* **Custom Protocol Encapsulation:** Manual framing of encrypted payloads wrapped in verifiable magic headers before UDP transmission.  
* **Payload Obfuscation:** A modular cryptography layer (currently implementing a fast XOR cipher).  
* **Strict Separation of Concerns:** Highly modular C architecture separating the event loop, socket management, kernel I/O, and cryptography.

## **Project Architecture**

The codebase is organized into small, decoupled modules, each with a single responsibility.

| Module | Responsibility |
| :---- | :---- |
| main.c | Orchestration. Manages the epoll event loop, invokes event handlers, and maps data between the TUN and network bounds. |
| tun.c | Kernel interactions. Allocates the virtual interface via /dev/net/tun, configures it via shell utilities, and performs raw IP reads/writes. |
| udp.c | Network transport. Initializes IPv4 datagram sockets and abstracts the sendto/recvfrom logic. |
| event\_loop.c | I/O Multiplexing. Provides a clean wrapper around epoll\_create1 and epoll\_ctl. |
| header.c | Protocol framing. Prepends the 8-byte MAGIC\_HEADER to outgoing payloads for validation. |
| crypto.c | Data obfuscation. Handles byte-level encryption and decryption of intercepted packets. |
| config.h | Static configuration. Holds buffer sizes, port mappings, IP destinations, and cryptographic keys. |

## **Packet Flow**

vntun operates strictly asynchronously. The daemon sleeps at 0% CPU utilization until the kernel wakes it with an event on either the virtual TUN descriptor or the physical network socket.

### **Transmit Pipeline (Outbound)**

  \[Kernel Routing\]   
         │ (Raw IPv4 Packet)  
         ▼  
    tun\_read()          \<-- Reads raw packet from /dev/net/tun  
         │  
         ▼  
  Header Parsing        \<-- Drops non-IPv4; Drops unknown destinations  
         │  
         ▼  
     encrypt()          \<-- Scrambles the IP packet (XOR)  
         │  
         ▼  
 attach\_header()        \<-- Prepends "AAKARSH\!" magic protocol header  
         │  
         ▼  
 send\_udp\_msg()         \<-- Encapsulates in UDP and fires to remote peer

### **Receive Pipeline (Inbound)**

  \[Physical Network\]  
         │ (UDP Packet)  
         ▼  
receive\_udp\_msg()       \<-- Reads raw packet from standard socket  
         │  
         ▼  
 Verify Magic Header    \<-- Drops packets with invalid headers instantly  
         │  
         ▼  
     decrypt()          \<-- Restores the raw IPv4 packet  
         │  
         ▼  
    tun\_write()         \<-- Injects raw IP packet back into local kernel  
         │  
         ▼  
  \[Kernel Routing\]

## **Repository Structure**

vntun/  
├── Makefile            \# Standard build automation rules  
├── include/            \# Shared headers defining API boundaries  
├── src/                \# Implementation files for all modules  
├── scripts/            \# Helper scripts (e.g., tun\_conf.sh) for OS-level config  
├── obj/                \# Compiled object files (Generated)  
└── bin/                \# Output binaries (Generated)

## **Build and Execution**

### **Dependencies**

* Linux Operating System (requires /dev/net/tun and epoll)  
* GCC (GNU Compiler Collection)  
* Make

### **Compilation**

Simply run make in the root directory. The build system will compile the object files and output the executable to bin/.  
make

### **Execution**

Running the daemon requires superuser privileges to allocate the virtual TUN interface. Ensure that ./scripts/tun\_conf.sh exists and is executable for network configuration.  
sudo ./bin/vntun

## **Configuration**

Settings are currently compiled statically via include/config.h. To test against a different peer, modify these definitions and recompile:

* **TUN Settings:** TUN\_NAME, TUN\_IP, TUN\_PREFIX define the virtual subnet space.  
* **UDP Transport:** LOCAL\_IP / LOCAL\_PORT define the binding listener. PEER\_IP / PEER\_PORT define the physical target destination.  
* **Protocol Rules:** MAGIC\_HEADER defines the packet validation signature.  
* **Cryptography:** MY\_KEY sets the shared secret used for payload obfuscation.

## **Current Limitations**

* **XOR Cryptography:** The current payload obfuscation uses a basic XOR cipher intended purely for learning bitwise operations. It is not cryptographically secure.  
* **Static Peer Mapping:** vntun currently routes traffic for a single hardcoded destination IP.  
* **No Replay Protection:** Protocol headers are validated, but sequence numbers are not yet implemented to reject duplicated/intercepted packets.  
* **Compile-time Configuration:** Network topologies cannot be changed without rebuilding the binary.

## **Future Roadmap**

* **AES Integration:** Replace the XOR obfuscation with a standard stream cipher (e.g., ChaCha20 or AES) using a library like OpenSSL or libsodium.  
* **Dynamic Configuration:** Parse a .conf or .json file on startup to allocate routing rules, bypassing config.h.  
* **Replay Protection:** Implement a seq\_num field in the protocol header and maintain a sliding window of validated packet sequences.  
* **Multi-Peer Routing:** Introduce a hash map connecting Virtual IPs to Physical Socket Addresses, allowing the daemon to act as a hub for multiple nodes.

## **Design Philosophy**

* **Fail-Fast Validation:** The system aggressively drops malformed packets (e.g., bad IPv4 version, wrong destination, missing magic headers) before expensive cryptography operations are triggered, conserving CPU cycles.  
* **Zero-Allocation Fast Path:** Dynamic memory (malloc/free) is heavily avoided inside the main event loop. Buffers are statically allocated on the stack to prevent memory leaks and fragmentation under heavy network load.  
* **Single-Responsibility C Files:** Global state is isolated. For instance, the main event loop knows nothing about the underlying socket implementation; it simply asks udp.c to send a payload.

## **Learning Objectives Addressed**

Reading and maintaining this repository demonstrates practical knowledge of:

* Linux kernel networking subsystems and userspace device interaction.  
* The mechanics of IP routing, Subnets, and Network Address Translation mapping.  
* Asynchronous systems programming and avoiding thread-locking bottlenecks via epoll.  
* Safe memory handling, struct packing (\_\_attribute\_\_((packed))), and binary parsing in C.