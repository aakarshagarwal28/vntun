# learning_log.md
### A personal engineering journal — VNTUN, a Layer 3 VPN daemon in C

---

> *Written as I went. Messy in places. That's kind of the point.*

## Why I Even Started This

I'd been doing networking theory for a while. Subnets, ARP, routing, all of it. But something about it felt like I was memorizing diagrams instead of actually understanding what was happening.

So I decided to build a Layer 3 VPN daemon in C. From scratch. Not stitching together `libssl` or calling `openvpn`. I wanted to understand every byte. Every header. Every syscall.

The goal wasn't just a working VPN. It was to understand Linux networking the way someone who builds tools understands it — not just someone who uses them.

One constraint I set for myself early: every module should own exactly one responsibility. I didn't want a monolithic `main.c` that does everything. I wanted to be able to open any single file and understand exactly what it does without reading any other file. That constraint ended up shaping every decision I made.

---

## Build System — Makefile

I'd compiled C before but always in the most embarrassing way — one `gcc main.c -o out` on the command line and pray.

For this project I wanted it to feel like a real C project. So I started with a Makefile and actually learned it properly this time.

The first thing that clicked was pattern rules. Instead of listing every object file manually, you can say "for any `.c` file in `src/`, produce a `.o` file in `obj/` and here's how". That's the `$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c` rule in the Makefile. Before this I would have written out every single file by hand and then forgotten to update it when I added new files.

The second thing was `wildcard` expansion. `$(wildcard $(SRC_DIR)/*.c)` just gives you every `.c` file automatically. And then `patsubst` transforms that list into the corresponding object file paths. So the Makefile basically knows about new source files the moment you create them. No manual updates.

The directory structure — `include/`, `src/`, `obj/`, `bin/` — was something I borrowed from actual open-source C projects. It makes the project feel organized and also forces you to think about what's a public API (goes in `include/`) versus what's an implementation detail (stays in `src/`).

Having `make clean` delete `obj/` and `bin/` completely and `make all` regenerate them from scratch was also useful. Several times during debugging I suspected stale object files and being able to just nuke and rebuild in one command was reassuring.

---

## Endianness — The First "Wait, What?" Moment

*Exercise 1: The Endianness Inspector*

This was the first genuinely confusing moment of the project.

I'd heard of endianness before. Big endian, little endian. I knew the rough idea. But I hadn't actually run into it as a concrete problem.

The exercise made it real. When you have a multi-byte value like a port number or an IP address length field, different CPU architectures store those bytes in different orders. My machine (little-endian, like most x86 systems) stores the least significant byte first. Network protocols store multi-byte values big-endian (most significant byte first). So if I just read a 16-bit port number from a packet as a regular `uint16_t`, I'd get a completely wrong value.

That's what `ntohs()` and `ntohl()` are for. "Network to host short" and "network to host long". You call them when reading multi-byte fields from network data and the reverse (`htons()`, `htonl()`) when writing them.

What I didn't expect was how subtle this becomes with hexadecimal. When you're printing packet bytes in hex and trying to interpret them, you have to keep remembering that the bytes on the wire might be in the opposite order from what your intuition says. That caused confusion a few times before it properly settled in my head.

The mental model I ended up with: the wire has its own byte order. Your CPU has its own byte order. Every time you cross that boundary, you call the appropriate conversion function. Always. Without exception.

---

## UDP Communication

The first real milestone of the project.

I'd used sockets in Python before but never in C. The C socket API is significantly more explicit. You're not calling some `socket.send()` method — you're filling out `struct sockaddr_in` by hand, calling `bind()`, `sendto()`, `recvfrom()`. Every step is visible.

The first thing I got confused about was the difference between the local endpoint and the peer endpoint. When you call `bind()` you're saying "this socket lives at this IP and port on my machine." When you call `sendto()` or configure a peer address, you're saying "when I send, deliver to this IP and port."

At one point I wrote the socket initialization but forgot to return the socket file descriptor from `udp_init()`. That function just returned a status code. Which means the rest of the program had no handle to the socket. Everything would have been silent garbage. When I caught it, I realized: a socket is not a single transmission. It's a persistent endpoint. The file descriptor represents the whole lifetime of that socket. The event loop needs that fd to stay alive for the entire duration of the daemon. That single realization changed how I thought about the udp module — `udp_init()` has to return the `fd`, not a success/failure code.

The loopback test was the first real confidence moment. Send a message on port 9191, receive it on port 9191 on localhost. Seeing `hello` arrive on the receive side felt disproportionately satisfying for how simple it was. But it told me: the networking layer works. Build on top of it.

---

## TCP vs UDP — A Doubt I Couldn't Ignore

*Exercise 3: The UDP Ping-Pong Server*

When I built the TCP server first (as part of exercise 3), I finally understood what the three-way handshake is actually doing. It's not ceremony. It's establishing a shared state. Both sides confirm that they can reach each other, agree on sequence numbers, and commit to guaranteeing delivery. `socket → bind → listen → accept → read → write → close`. Every step serves that goal.

Then I moved to UDP. `socket → bind → sendto/recvfrom → close`. No handshake. No sequence numbers. Fire and forget.

This is where I had a genuine doubt I couldn't just brush past: if SSH (which requires commands to arrive in exact order) runs over our VPN, and our VPN uses UDP which doesn't guarantee order, what happens if the order of TCP segments changes?

After sitting with this for a while, I understood the layering. The VPN is a transport at the IP layer. It transports IP packets. TCP, which SSH runs on top of, has its own sequence numbers and reordering logic. The TCP layer inside the tunnel will handle out-of-order arrival of packets the same way it handles out-of-order arrival on any network — by buffering and reordering before delivering to the application.

So the VPN doesn't need to care about ordering at all. The kernel's TCP stack handles that. The VPN just needs to get the packets from one side to the other, eventually. Which UDP does fine for this use case.

What matters for VPN is low overhead and simplicity. We're not trying to guarantee delivery at the VPN layer — we're just building an encrypted pipe. TCP inside the tunnel will handle its own guarantees. That's why using UDP for the tunnel makes sense.

---

## Packet Sizes and Buffer Management

This was probably the single biggest conceptual hurdle in the entire project. It took longer to resolve than I expected and once it clicked, it simplified everything downstream.

Initially I kept trying to decide: what is the right buffer size? Should I allocate 512 bytes? 1000? 2048? The full IPv4 limit of 65535?

The confusion came from thinking about buffers and packet sizes as the same thing. They're not. The buffer is just storage you allocate. The actual packet size is whatever the kernel tells you through the return value of `read()`, `recvfrom()`, or similar calls.

Once I stopped trying to figure out "what size should my packets be" and started thinking in terms of `(buffer, length)` pairs, everything became simpler.

Every function in the codebase follows this pattern:

```
input:  (unsigned char *buffer, size_t length)
output: (unsigned char *buffer, size_t new_length)
```

The buffer is just storage. The length is the ground truth. `read()` tells you how many bytes are valid. That number propagates through the system. `encrypt()` takes `n` bytes and produces `n` bytes. `attach_header()` takes `n` bytes and produces `n + 8` bytes. `send_udp_msg()` sends exactly `len` bytes.

Nothing in the pipeline needs to guess. Nothing computes length from content. The length is always passed explicitly.

This became the foundational discipline of the entire codebase. Before this clicked, I was making mistakes with every new function. After it clicked, every new function basically wrote itself.

Also from Exercise 4 — when reading from a file or stream in chunks of 512, you'd see `512, 512, 247, 0`. That pattern of "full chunks then a partial last chunk" made it visually obvious how `(buffer, length)` propagation works. The last read isn't broken — it's just shorter, and the length tells you exactly how much to use.

---

## Strings vs Binary — The Biggest Mindset Shift

For a while after starting this project, I was still subconsciously thinking in C strings. I used `strcpy()`. I used `strlen()`. I thought about null terminators.

Then I actually sat down and thought about what a packet is.

A packet is not text. It is a sequence of bytes. It can have `0x00` in the middle. It can have `0xFF`. The bytes have no special terminator. `strlen()` on a packet would give you completely wrong results if there's a zero byte anywhere inside the IP header, which there definitely is.

From that point on, I completely restructured my thinking. Packets are binary objects. The entire codebase uses:

```c
unsigned char *buffer
size_t length
```

Not `char *`. Not `strlen()`. Not null-terminated anything.

`unsigned char` matters specifically because it avoids sign-extension issues when you do things like `buffer[0] >> 4` to extract the IP version. With signed `char`, right-shifting a byte with the high bit set gives you implementation-defined behavior. With `unsigned char` it's always a logical shift.

I also had to unlearn the habit of thinking about buffers as strings to be printed. These buffers are memory regions containing structured binary data. The only way to interpret them correctly is to cast them to the right struct (like `struct iphdr`) or to index into them with explicit byte offsets. `printf("%s", buffer)` would be nonsense for a packet.

---

## Struct Padding and `__attribute__((packed))`

*Exercise 2: The Raw Header Mapper*

This one surprised me. I knew structs in C had alignment requirements, but I hadn't actually thought about what that means for packet parsing.

The issue is this: when the compiler lays out a struct in memory, it adds padding between fields to align them to their natural alignment boundaries. This is good for CPU performance — aligned reads are faster. But it's catastrophic if you're trying to overlay a struct on raw packet bytes, because the bytes in a packet are packed with no padding. Every byte is exactly where the protocol says it is.

So if you take a struct that has a 1-byte field followed by a 2-byte field, the compiler might add 1 byte of padding between them to align the 2-byte field. Now if you cast a raw packet buffer to that struct, the 2-byte field reads from the wrong byte offset. You get completely wrong values.

The fix is `__attribute__((packed))`. It tells the compiler: don't add padding. I accept the CPU inefficiency. I need the struct layout to match the wire format exactly.

This is why `struct iphdr` from `<netinet/ip.h>` is defined with packed semantics. Without that guarantee, using it to parse raw packet bytes would give you garbage.

There was also a bigger question I had during this exercise: why are we attaching our own header at the front of the encrypted payload? What's the purpose of the magic header? Couldn't we just skip it?

The answer is identification and safety. When a UDP packet arrives on our port, we need to know it came from our own VPN daemon and not from some other process that also happens to be sending UDP to that port. The magic header `AAKARSH!` acts as a recognizable prefix — a handshake token that says "this packet belongs to the VPN, process it." If the header doesn't match, we drop the packet.

Is it a security mechanism? Not really. An attacker who knows the magic header can craft a packet that passes the check. But that's not what it's for. It's identification, not authentication. The actual security comes from the encryption — if someone sends a packet without knowing the key, decryption produces garbage and the kernel rejects the malformed IP packet. The header just prevents us from accidentally trying to decrypt things that were never meant for us.

---

## The TUN Device — "Everything on Linux is a File"

*Exercise 5: The Ghost Interface*

This was the phase where the phrase "everything on Linux is a file" stopped being a slogan and started being something I actually understood.

`/dev/net/tun` is a character device. It's represented as a file in the filesystem. You open it with `open()` just like any other file. But what you're really doing is asking the kernel: "I want to interact with the TUN/TAP driver."

The workflow is:
1. `open("/dev/net/tun", O_RDWR)` — get a file descriptor to the driver
2. Fill out `struct ifreq` with the interface name (`tun0`) and flags (`IFF_TUN | IFF_NO_PI`)
3. `ioctl(tun_fd, TUNSETIFF, &ifr)` — tell the kernel to create the interface and associate it with this fd

After that, anything that the kernel routes to `tun0` can be read from `tun_fd`. And anything you write to `tun_fd` gets injected into the kernel's network stack as if it arrived from `tun0`.

`IFF_NO_PI` was something I had to look up. By default, the TUN driver prepends a 4-byte "packet information" header to each packet. That header describes the protocol type. If I left that on, every packet would have 4 extra bytes at the front that I'd have to strip. Since I'm only handling IPv4 anyway, I just turned it off with `IFF_NO_PI`. Cleaner.

The shell script for configuring the interface (`tun_conf.sh`) was a deliberate choice. I tried embedding the `ip addr add` and `ip link set` commands using `system()` with inline strings, but that got messy and hard to iterate on. Moving it to a script I could edit independently was cleaner. The C code just calls the script with the right parameters.

In the terminal, after running the daemon, doing `ip link` would show `tun0` with status `UNKNOWN` instead of `UP` like physical interfaces. That "UNKNOWN" puzzled me for a bit before I realized it's because TUN interfaces don't have a physical carrier signal. The kernel marks real interfaces UP/DOWN based on whether a physical cable is connected. TUN interfaces don't have that, so the kernel reports `UNKNOWN`. It's not broken — it's the expected state for a virtual interface.

---

## Reading from TUN — What Does `read()` Actually Return?

This question sounds obvious now but it wasn't when I first encountered it.

When you `read()` from a regular file, you get bytes of that file starting from the current offset. Simple.

When you `read()` from the TUN file descriptor, I initially assumed you'd get some kind of arbitrary buffer of raw bytes with some kernel-specific framing. Maybe the kernel wraps it somehow. Maybe you get partial packets. I genuinely didn't know.

After printing packet bytes and inspecting them, I eventually realized: what you get back is a complete, valid IPv4 packet. Exactly as the kernel constructed it. No extra framing (because `IFF_NO_PI`). No partial reads. The return value of `read()` is the exact number of bytes in that packet.

This realization changed everything. I had been thinking about how I might need to reconstruct packets, accumulate partial reads, handle framing. None of that. The kernel hands me a complete, valid IPv4 packet and `read()` returns its exact size.

From that point I started thinking about the TUN device differently. Instead of some raw byte stream, it's a queue of complete IP packets. Each `read()` gives you one packet. Each `write()` injects one packet. Clean, atomic, perfectly aligned with how IP networking works.

---

## IPv6 Noise and Packet Filtering

Almost immediately after getting the TUN interface up and my `read()` loop running, I started seeing 48-byte packets I hadn't expected.

I printed the first byte of the buffer. Then I checked the IP version field: `buffer[0] >> 4`. For IPv4 packets this should be `4`. For these 48-byte packets it was `6`.

IPv6 neighbour discovery. The kernel's network stack generates these automatically — they're part of how IPv6 nodes discover each other on a network. Since I'd just brought up a new interface, the kernel started generating this background traffic. Nothing was wrong. But my code needed to handle it.

The fix is simple: check `buffer[0] >> 4 == 4` before doing anything else. If it's not IPv4, drop it and continue waiting. The codebase does exactly this in `handle_tun_event()`:

```c
if ((tun_buffer[0] >> 4) != 4) {
    printf("[DROP] Not an IPv4 packet\n\n");
    return -1;
}
```

This was also a useful early reminder that the TUN interface is not private. Other processes and kernel subsystems will generate traffic on it. You can't assume every packet is something your application created. You have to filter.

---

## IPv4 Packet Parsing

Once I was confident I had an IPv4 packet in the buffer, I wanted to actually read it — not just treat it as opaque bytes.

`struct iphdr` from `<netinet/ip.h>` is the standard C struct for an IPv4 header. After the `IFF_NO_PI` check, the buffer starts directly at the IP header. So casting `(const struct iphdr *)buffer` gives you direct field access.

The fields I ended up using:

- `ip->daddr` — destination address (as `in_addr_t`, i.e., a 32-bit integer in network byte order)
- `ip->saddr` — source address
- `ip->protocol` — the next-layer protocol (TCP=6, UDP=17, ICMP=1)
- The version field isn't directly in `iphdr` as a standalone field — it's packed with the IHL — hence the `buffer[0] >> 4` approach.

`inet_ntop()` converts a binary `in_addr_t` into a human-readable string like `"10.99.0.2"`. I used that for printing and also for the destination check:

```c
if (strcmp(dst, "10.99.0.2") != 0) {
    printf("[DROP] Destination not handled by VPN\n\n");
    return -1;
}
```

That hardcoded destination check was a deliberate prototype decision. For the single-machine loopback test, I only wanted to handle packets going to `10.99.0.2`. Everything else gets dropped. It's not production-ready but it made the prototype much easier to reason about.

Parsing a real IPv4 header and printing the source and destination addresses — seeing `src: 10.99.0.1 -> dst: 10.99.0.2` in my terminal from a real packet I'd generated — was one of those moments where theory and practice snapped together.

---

## Packet Ownership and Checksums

This took me a while to fully internalize and it's one of the most important realizations in the whole project.

I started out thinking I might need to modify the IP packet as it passes through the VPN. Maybe I'd need to update the destination IP, or recalculate the IP checksum, or rebuild the header in some way.

Then I thought more carefully about what a VPN actually does.

The Linux kernel constructed that IPv4 packet. It set all the fields. It computed the checksum over those exact bytes. If I modify any byte in the packet, the checksum becomes invalid, and when the packet arrives at the receiving kernel, it will be rejected as corrupt.

But here's the thing: I don't need to modify anything. The packet was created correctly by the sending kernel. My job is to transport it, unchanged, to the receiving kernel. The receiving kernel will process it as if it had just arrived from a physical network.

So the pipeline became:

```
Kernel constructs packet
      ↓
tun_read() — I receive complete valid packet
      ↓
encrypt() — bytes are transformed, but length preserved
      ↓
UDP transmit
      ↓
UDP receive
      ↓
decrypt() — bytes restored to original
      ↓
tun_write() — kernel receives identical packet
      ↓
Kernel processes it — checksum is still valid
```

Encryption transforms bytes but if decryption restores them exactly, the checksum survives. I never touch the checksum. I never recompute it. Because I never need to.

This mental model — that I'm a transparent pipe transporting owned packets, not a system that constructs or modifies packets — simplified the design enormously.

---

## The Magic Header

Once I had encryption working, I needed a way to identify VPN packets on the receiving side.

The design is simple: prepend 8 bytes to the encrypted payload before sending over UDP. Those 8 bytes are `AAKARSH!` — the magic header defined in `config.h`.

On the receiving side, before doing anything else, compare the first 8 bytes against the magic header. If they don't match, drop the packet.

```c
if (memcmp(payload, MAGIC_HEADER, 8) != 0) {
    printf("[DROP] Invalid VPN header\n\n");
    return -1;
}
```

The `attach_header()` function in `header.c` does:

```c
memcpy(payload, MAGIC_HEADER, 8);
memcpy(payload + 8, enc, n);
return 8 + n;
```

This was the first time I genuinely needed pointer arithmetic to feel natural. `payload + 8` moves 8 bytes forward in the buffer, right past the header. Then we copy the encrypted packet there. The result is a contiguous buffer: 8 bytes of header immediately followed by the encrypted packet bytes. Total size: `n + 8`.

The function returns the new size, following the `(buffer, length)` discipline from throughout the rest of the codebase.

---

## Pointer Arithmetic and `memcpy()`

There was a period in this project where pointer operations were a constant source of bugs and confusion. Worth writing down explicitly.

The crucial distinction I had to internalize:

```c
ptr2 = ptr1;          // changes where ptr2 points — no bytes are copied
memcpy(dst, src, n);  // copies n bytes from src to dst — ptr values unchanged
```

Before this was solid in my head, I wrote several bugs where I'd do `buffer = encrypted_buffer` expecting the bytes to be there, and then wonder why nothing worked. I was moving a pointer, not moving memory.

The other thing that took time: `char *` arithmetic. When you write `payload + 8`, and `payload` is `unsigned char *`, you're moving 8 bytes forward. This is because pointer arithmetic moves by `sizeof(*pointer)`. For `unsigned char *`, `sizeof(unsigned char)` is 1, so `+ 8` is exactly 8 bytes. If `payload` were `int *`, `+ 8` would move 32 bytes. Getting this right was important for the header attachment logic.

After getting burned enough times, my rule became: always use `memcpy()` when you want bytes to move. Use pointer arithmetic when you want to address a different part of an existing buffer. Never confuse the two.

---

## XOR Encryption

I implemented repeating-key XOR encryption. It's in `crypto.c` and it's genuinely simple:

```c
for (size_t i = 0; i < len; i++) {
    en[i] = data[i] ^ KEY[i % klen];
}
```

For each byte of the input, XOR it with the corresponding byte of the key (cycling through the key with modulo).

The reason it's symmetric — the same function for encryption and decryption — is because XOR has a nice property: `A ^ B ^ B = A`. So if you XOR with the key to encrypt, and XOR with the same key to decrypt, you get the original bytes back. Which is why `handle_udp_event()` calls `encrypt(enc, n, dec)` to decrypt — it's the same operation.

I spent some time wondering why this is considered weak cryptography. Is XOR itself weak? No, actually. XOR with a truly random key that's as long as the message (a one-time pad) is information-theoretically unbreakable. The weakness is the repeating key.

With a repeating key, if an attacker collects enough ciphertext, they can use frequency analysis and other statistical attacks to recover the key. The key repeats, so patterns in the plaintext create patterns in the ciphertext. That's the vulnerability.

But for the purposes of this project — understanding the architecture of a VPN, learning how encryption slots in as a module — repeating-key XOR is perfect. The API is the important part: takes `(buffer, length)`, returns `(buffer, length)`. Encryption is a pure byte transformation. It doesn't know about UDP. It doesn't know about TUN. It doesn't know about IP headers. It just transforms bytes.

That clean separation is what matters for the architecture.

---

## Configuration Management

Hardcoded IPs and keys scattered through the source code are a nightmare for two reasons: changing them requires editing multiple files and risking introducing bugs, and you can't commit secrets to git safely.

I moved everything into `config.h`:

```c
#define LOCAL_IP      "127.0.0.1"
#define LOCAL_PORT    9191
#define PEER_IP       "127.0.0.1"
#define PEER_PORT     9191
#define MY_KEY        "THE_BEATLES"
#define TUN_NAME      "tun0"
#define TUN_IP        "10.99.0.1"
#define TUN_PREFIX    24
#define MBS           2048
#define MAGIC_HEADER  "AAKARSH!"
```

The pattern for real projects is to have a `config_example.h` committed to the repo — showing structure without actual secrets — and have `config.h` listed in `.gitignore`. Anyone who clones the repo copies the example and fills in their own values.

For this prototype everything is localhost so nothing is actually secret. But building the habit of keeping configuration separate from implementation is worth doing even in a learning project.

`MBS = 2048` as the buffer size was a deliberate choice. IPv4 packets can theoretically be up to 65535 bytes, but in practice Ethernet MTU is 1500 bytes and most packets are well below that. 2048 is comfortably larger than any realistic packet I'd encounter on localhost while still being a sensible allocation. Not too small to truncate real packets, not so large it wastes stack space.

---

## Git — Detached HEADs and Checkpoints

I tried to use git properly for this project. Not just as a backup, but as a way to create checkpoints I could actually return to.

The things I learned:

**Detached HEAD** — this happens when you `git checkout` a specific commit hash instead of a branch name. You're "at" that commit but not on any branch. Changes you make here won't be tracked unless you explicitly create a branch from that point. This initially alarmed me until I understood what it meant. Now it's useful: I can go look at how the code looked at an earlier point, experiment, and then either commit to a new branch or discard and go back to `main`.

**Branching from old commits** — `git checkout -b new-feature <commit-hash>` creates a new branch starting from that old commit. Useful for trying an alternative approach without disturbing main development.

**Comparing arbitrary commits** — `git diff <hash1> <hash2>` shows exactly what changed between two points. I used this to understand how the architecture evolved between the initial UDP-only version and the final event-loop version.

The mental model I settled on: every meaningful milestone gets a commit. Each commit is a checkpoint I can return to. The history of the project is a record of my understanding deepening, not just a backup of the final state.

---

## epoll and Event-Driven Architecture

*Exercise 7: The Blocking Trap* and *Exercise 8: The epoll Multiplexer*

The blocking trap exercise was enlightening. I had a UDP server and after `recvfrom()` I wanted to also read from stdin. But the program just sat there. `recvfrom()` blocked forever waiting for a packet, and stdin never got a chance.

That's the core problem that multiplexing solves. If you have two sources of I/O and either one can arrive at any time, you can't just block on one of them. You need a way to wait on both simultaneously and react to whichever one becomes ready first.

The three main approaches in Linux:

**select()** — You pass in three sets of file descriptors (readable, writable, exceptional). The kernel scans all of them and returns which are ready. The interface uses bitmaps, which I could directly link to my competitive programming background. The problem is that it scans every descriptor every call. If you have 10,000 file descriptors and only 2 are active, you're doing O(n) work per iteration where n is the total number of descriptors.

**poll()** — Same fundamental behavior, slightly cleaner API. Uses an array of `struct pollfd` instead of bitmasks. Still O(n) scanning.

**epoll()** — The Linux-native solution. You register interest once with `epoll_ctl()`. The kernel maintains an internal data structure (implemented as a red-black tree internally) tracking which descriptors you care about. Then `epoll_wait()` returns only the descriptors that are actually ready. O(1) for the wait itself, regardless of how many descriptors you've registered.

The insight that made epoll click for me was thinking about it as a data structure problem. This is exactly the kind of optimization I'd think about in competitive programming: instead of scanning an entire array every iteration to find active elements, maintain a data structure that tells you directly which elements matter. The kernel is solving the same problem.

For my VPN daemon specifically — only 2 file descriptors, `tun_fd` and `udp_fd` — the O(1) vs O(n) difference is irrelevant. With n=2, O(2) ≈ O(1). But using epoll is the right pattern regardless. It's what production systems use, and understanding why it's better is more valuable than using the simpler option just because n happens to be small.

---

## The Event Loop Maps to DSA

This connection genuinely surprised me and made epoll much less intimidating.

When I first read the epoll man pages, I kept getting confused by the structure — `epoll_create1()`, `epoll_ctl()` with `EPOLL_CTL_ADD`, the `epoll_event` array in `epoll_wait()`. The API has a lot of moving parts.

Then I mapped it to data structures I already understood:

The **interest list** (everything you've registered with `epoll_ctl`) behaves like a set — you add to it, you remove from it, and membership is what matters. Internally it's a red-black tree, which gives O(log n) for add/remove.

The **ready list** (what `epoll_wait()` returns) behaves like a queue — the kernel enqueues descriptors that become ready, you dequeue them in `epoll_wait()`.

So the event loop isn't magic. It's:
- Add your interesting fds to a set
- Wait until any of them have data
- Process whichever ones do
- Repeat

The select-by-scanning approach is like linear search. The epoll approach is like using a hash set or balanced tree. Same problem, different data structure, different complexity. 

Once I saw it this way the whole API felt natural.

---

## Level Triggered vs Edge Triggered

The difference took me longer to appreciate than I expected.

**Level Triggered (default)** — `epoll_wait()` returns a file descriptor as ready as long as there is unread data. If I read some data but leave more behind, the next call to `epoll_wait()` will return the same fd again. It keeps notifying me until the data is gone. This is forgiving — if I miss some data, I'll get another chance.

**Edge Triggered (`EPOLLET`)** — `epoll_wait()` only notifies me when the state of the fd *transitions* from "no data" to "has data." If I read some data but not all of it, and no new data arrives, I will not get another notification. I have to completely drain the fd in one shot, usually with a `while` loop that reads until `EAGAIN`.

For my VPN daemon, level-triggered is the obvious choice and for a specific reason: each `tun_read()` call reads exactly one complete IP packet, and each `recvfrom()` call reads exactly one complete UDP datagram. There's no situation where I'd read partial data and leave the rest behind. So I'll never miss anything with level-triggered mode.

Edge-triggered is more efficient for high-throughput servers because it reduces the number of wake-ups. But it requires significantly more careful programming — you need non-blocking fds and a drain loop — and the additional complexity wasn't justified here.

Understanding *why* level-triggered naturally fits my architecture (one event = one complete packet = one complete handling) was more valuable than just knowing the flag names.

---

## The `tun_write()` Realization

This was one of those moments where my initial mental model was completely wrong and the correction changed everything.

I was worried about infinite loops. The logic went like this: if I receive a UDP packet, decrypt it, and write the resulting IP packet to `tun_fd`, doesn't that put it back on the TUN interface? And then doesn't `tun_read()` pick it up again? And then wouldn't I encrypt it and send it back out? Infinite loop?

I experimented. I wrote the packet to `tun_fd` and watched what happened. No loop.

Then I understood the distinction. TUN has two paths:

- **Transmit path** (outgoing): When a process on this machine sends a packet destined for the TUN interface's subnet, the kernel routes it through `tun0` and delivers it to userspace via `read(tun_fd)`. This is how packets enter my daemon from the local machine.

- **Receive path** (incoming): When I `write(tun_fd, ...)`, the kernel treats those bytes as a packet that just arrived from the network via `tun0`. It goes into the kernel's receive processing — routing table lookup, delivery to the appropriate socket, etc. It does NOT go back out through the transmit path.

`tun_write()` injects a packet into the kernel's receive path. It's as if the packet arrived from outside. Not as if I'm sending from this machine.

This is exactly the right behavior for a VPN. The VPN receives an encrypted packet from the remote peer, decrypts it, and hands the resulting IP packet to the local kernel as if it had just arrived from the network. The kernel then delivers it to the appropriate local application. No loop, no problem.

---

## Project Architecture — Module Separation

The module structure I ended up with:

| File | Responsibility |
|------|---------------|
| `src/udp.c` | Everything related to UDP sockets — init, send, receive, cleanup |
| `src/tun.c` | Everything related to the TUN device — init, config, read, write, parse |
| `src/crypto.c` | Pure byte transformation — encrypt/decrypt |
| `src/header.c` | VPN header attachment and verification |
| `src/event_loop.c` | epoll creation and fd registration |
| `src/main.c` | Orchestration — initialize modules, run event loop, dispatch events |

Every module has one job. `crypto.c` knows nothing about networking. `udp.c` knows nothing about IP parsing. `tun.c` knows nothing about encryption. If I want to swap XOR for AES, I only touch `crypto.c`. If I want to change the TUN interface name, I change `config.h` and nothing else needs updating.

The headers in `include/` are the public interfaces of each module. They define what you can do with each module without telling you how it's done. `tun.h` exports `tun_init()`, `tun_read()`, `tun_write()`, etc. You don't need to read `tun.c` to use the TUN module.

This separation also meant each module could be developed and tested somewhat independently. I had UDP working before I touched TUN. I tested the header format standalone before connecting it to the encryption. The event loop was understood conceptually before I wrote a single line of it.

---

## main.c — Pseudocode That Actually Compiles

This is the part I'm probably most proud of.

From the beginning I wanted `main.c` to read almost like pseudocode. Not like a tangle of networking code and business logic. The entire main function should describe the flow of the daemon without containing any implementation details.

```c
int main() {
    udp_fd = udp_init(LOCAL_IP, LOCAL_PORT, PEER_IP, PEER_PORT);
    tun_fd = tun_init();
    tun_config();

    int epfd = epoll_init();
    epoll_register(epfd, tun_fd);
    epoll_register(epfd, udp_fd);

    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        // dispatch to handle_tun_event() or handle_udp_event()
    }
}
```

You can read that and understand what the daemon does without knowing anything about TUN devices or epoll internals. Initialize. Register. Wait. Dispatch.

The two handler functions, `handle_tun_event()` and `handle_udp_event()`, each follow the same pattern. Read data. Check it. Transform it. Send it. Each step is a call to a module function with a comment explaining what's happening. The handlers don't do networking — they orchestrate module calls.

I read somewhere that `main.c` for well-designed systems should look like this. I understood the idea but it only became real when I actually built it this way and felt how much easier the code was to reason about.

---

## The Ping Experiment

After the event loop was running and both handlers were working, I tested the full pipeline on a single machine.

I sent a ping to `10.99.0.2`:

```
ping 10.99.0.2
```

The kernel generates an ICMP Echo Request destined for `10.99.0.2`. My routing table routes that subnet through `tun0`. The packet arrives in my daemon via `tun_read()`. The daemon checks: IPv4? Yes. Destination `10.99.0.2`? Yes. Encrypts it. Attaches header. Sends via UDP to `127.0.0.1:9191`.

Since it's the same machine, the UDP packet arrives at the same socket. The daemon's UDP handler picks it up. Verifies header. Decrypts. Writes back to `tun_fd` via `tun_write()`.

The packet is now injected back into the kernel's receive path on `tun0`. The kernel processes an ICMP Echo Request destined for `10.99.0.2`. But `10.99.0.2` isn't assigned to any interface on this machine. Nobody owns that address. So there's nobody to send an Echo Reply from.

Ping still "fails" — request times out.

But the pipeline works perfectly. I can see in the logs:

```
[TUN] Read N bytes from tun
[CRYPTO] Encrypted N bytes
[HEADER] Attached VPN header
[UDP] Sending packet...
[UDP] Packet sent successfully

[UDP] Received N+8 bytes
[HEADER] VPN header verified
[CRYPTO] Decrypted N bytes
[TUN] Writing packet back to kernel...
[TUN] Successfully injected N bytes
```

The full loop: TUN → encrypt → header → UDP → header verify → decrypt → TUN. All of it working. The only reason ping "fails" is that there's no second machine to send the reply. Which is not a VPN problem. It's a topology problem.

To get a real end-to-end test with actual ping replies, I'd need a second machine — like a Radxa board — with the daemon running and `10.99.0.2` assigned to its `tun0` interface. That would complete the picture.

---

## The Biggest Final Realization

This came toward the end and it fundamentally changed how I think about VPNs.

For most of the project I kept half-thinking about how the VPN handles different protocols. Does it need to understand SSH? Does it need to handle HTTP differently from ICMP? Does it need to know about TCP sequence numbers?

No. It doesn't. It doesn't understand any of those things. It never needs to.

The VPN transports IPv4 packets. That's it.

SSH runs on top of TCP. TCP runs on top of IP. The VPN operates at the IP layer. The VPN receives an IP packet, transports it across the tunnel, and delivers it to the remote kernel. What's inside that IP packet — whether it's TCP carrying SSH data, or ICMP carrying a ping, or UDP carrying DNS — is completely irrelevant to the VPN.

The remote kernel already knows how to handle TCP. It already knows how to handle ICMP. It doesn't need the VPN to explain anything. It just needs to receive the original IP packet, intact, and it will do the right thing.

The VPN daemon builds an encrypted pipe between two kernels. The kernels handle everything else.

This changes how I'll think about every networking layer I study going forward. Protocols don't understand each other. They trust each other to fulfill their contract. The IP layer trusts the link layer to deliver frames. TCP trusts IP to deliver datagrams. Applications trust TCP to deliver byte streams. The VPN trusts the kernel to process the IP packets it injects. Nobody knows or cares what's above or below them. That's the whole point of layered networking.

---

## Wireshark and Practical Network Observation

This was something I ran alongside the main exercises and it made a huge difference.

Actually watching a TCP three-way handshake in Wireshark — seeing the SYN, SYN-ACK, ACK sequence in real time as my TCP server ran — connected the theory to something visible in a way that reading about it never quite did.

I watched HTTP requests go out and come back. I watched my own `tun0` interface appear in `ip link` with that `UNKNOWN` status. I ran `ip route` and saw the routing table exactly as it's described in networking theory — directly connected networks, default gateway, all of it visible right there in the terminal.

`traceroute` was particularly satisfying. I'd read about TTL (Time To Live) manipulation to discover hops along a path. Running it and watching the hops print out one by one, seeing the actual IP addresses of the routers in between — it made the default gateway entry in `ip route` suddenly feel real.

`ip addr` and `ip link` became tools I actually understand now rather than commands I copy-paste from Stack Overflow. Seeing `tun0` listed alongside `eth0` and `lo`, understanding why it says `UNKNOWN` where eth0 says `UP` — these are things that only make sense once you've built something that creates interfaces.

---

## The Packet Flow — A Summary

After everything came together, the symmetry of the packet flow became one of the most satisfying things about the project:

**Transmit Path (local machine → remote machine):**
```
Local Application
        ↓
  Kernel / IP Stack
        ↓
    tun_read()          ← kernel routes packet through tun0
        ↓
   IPv4 check
        ↓
   Destination check
        ↓
    encrypt()
        ↓
  attach_header()
        ↓
  send_udp_msg()        ← fires encrypted packet over network
```

**Receive Path (remote machine → local machine):**
```
  receive_udp_msg()     ← encrypted packet arrives
        ↓
  memcmp(MAGIC_HEADER)  ← verify it's ours
        ↓
   encrypt() [decrypt]
        ↓
    tun_write()         ← inject into kernel receive path
        ↓
  Kernel / IP Stack     ← delivers to local application
        ↓
 Local Application
```

The fact that this is perfectly symmetric — every step on the transmit path has an inverse on the receive path, in reverse order — is not an accident. It's what proper encapsulation looks like. If you understand one direction, you understand the other.

---

## Future Improvements

Things I know about but haven't done yet, and why they matter:

**Exercise 10 — Checksum implementation**: I revised the theory but haven't implemented and tested it yet. Understanding how the IP checksum is computed (one's complement sum of 16-bit words in the header) would complete the picture of why encryption preserves it — because the cipher operates on bytes, not on the semantic meaning of those bytes.

**Exercise 6 — Windows TUN (WinTUN)**: Skipped for now. Windows has a completely different approach to virtual networking interfaces. Worth understanding for cross-platform VPN implementations, but not on the critical path.

**Real encryption**: XOR with a repeating key demonstrates the architecture but isn't secure. Replacing `crypto.c` with ChaCha20 or AES-GCM (with authenticated encryption, so the header verification is cryptographic) would be the production path. The key insight is that the interface would remain the same — `(buffer, length) → (buffer, length)` — and nothing else in the codebase would change.

**Multi-peer support**: Right now the peer is hardcoded in `config.h`. A proper VPN daemon would have a peer table, routing logic to decide which peer handles which subnet, and per-peer key management.

**Two-machine test (Radxa)**: The single-machine loopback test proved the pipeline works. An actual two-machine test — running the daemon on both machines, assigning `10.99.0.1` to one and `10.99.0.2` to the other, pinging across the tunnel — would be the satisfying final proof. That's where you'd see the kernel on machine B generate an ICMP Echo Reply, which would travel back through the tunnel to machine A, and ping would actually succeed.

**Error handling and robustness**: The current codebase handles errors by printing a message and returning -1. A production daemon would need reconnection logic, graceful shutdown on signals, logging, and handling of partial sends.

---

*Total time for this phase: roughly 1 to 1.5 weeks, mostly evenings after work. It was slow in places but the slowness was the point. Every hour of confusion that ended in understanding was worth more than reading a tutorial that explained it in five minutes.*

*The project isn't finished. But the foundations are solid and, more importantly, I understand why every line of it is there.*