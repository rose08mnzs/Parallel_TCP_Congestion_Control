# Parallel TCP File Transfer and Congestion Control
A multithreaded TCP file-transfer project in C++ with Linux congestion-control experiments using BBR and a custom RTT-based kernel module. Developed for USC EE 542 Laboratory 3, with experiments involving AWS networking and an emulated network path through VyOS.

## Overview
The client divides a file into chunks and sends them over parallel TCP connections. The server receives each chunk in a separate thread and writes its bytes at the specified file offset using `pwrite()`.

The project explores how parallel connections, network delay, packet loss, and TCP congestion control affect file-transfer performance.

## Features
- Parallel file transmission with a configurable number of client threads; the default is four.
- One TCP connection per file chunk.
- File-size announcement before data transmission.
- Chunk headers containing the chunk size and destination offset.
- Offset-based reconstruction of the destination file.
- A final DONE message and acknowledgment.
- Experiments with Linux BBR and a separate custom congestion-control module.
- Laboratory deployment involving AWS EC2, VPC networking, and VyOS.

## Source files
| File | Purpose |
| --- | --- |
| `client.cpp` | Splits and sends a file over parallel TCP connections. |
| `server.cpp` | Receives chunks and writes them into the output file. |
| `lab3_rttdiff.c` | Implements the custom Linux TCP congestion-control algorithm `lab3_rttdiff`. |
| `README.md` | Project overview, build instructions, usage, and limitations. |

The original lab report and `README.txt` are separate supporting documents. They are not needed to compile the applications.

## Transfer protocol
1. The client opens an initial connection and sends the total file size as a `uint64_t`.
2. It divides the file into chunks, assigning the remainder to the final chunk.
3. Each worker opens a connection and sends two `uint64_t` values in this order: **chunk size, start offset**.
4. The worker sends the chunk's bytes. A server worker writes them at the corresponding offset.
5. After its worker threads finish sending, the client opens another connection and sends `(0, 0)` as the DONE marker.
6. The server acknowledges the DONE marker and requests shutdown.

The header values use native integer representation, so the current protocol assumes compatible endpoint architectures.

## Requirements
- Linux with POSIX sockets and `pwrite()` support.
- A C++17 compiler and POSIX threads.
- Network connectivity between client and server.
- For kernel-module experiments: matching Linux kernel headers, kernel-module support, `make`, and administrator access.
- For BBR experiments: a Linux kernel with BBR available.

Windows Command Prompt can be used to publish this repository with Git. Build and run these applications in Linux; the kernel module requires a suitable Linux kernel environment.

## Build the applications
On Ubuntu or Debian:
```bash
sudo apt-get update
sudo apt-get install -y build-essential
g++ -O2 -std=c++17 server.cpp -o server -pthread
g++ -O2 -std=c++17 client.cpp -o client -pthread
```

## Run a transfer
Start the server:
```bash
./server 5001 received.bin
```

The output file is created or truncated when the server starts. Use a different path from the input file during local testing.

In another terminal on the same machine, create a 64 MiB test file and run the client:
```bash
dd if=/dev/urandom of=input.bin bs=1M count=64
./client 127.0.0.1 5001 input.bin 4
```

For separate machines, replace `127.0.0.1` with the server's reachable **IPv4 address**:
```bash
./client SERVER_IPV4 5001 /path/to/input.bin 64
```

Replace `SERVER_IPV4` before running. Permit the selected TCP port through the relevant firewall and AWS security-group rules. Run one transfer at a time and restart the server before each new transfer.

Use a nonempty input file and a positive thread count no larger than the file's byte count. Invalid thread counts and empty chunks are not handled safely by the current implementation.

## Verify the received file
After all bytes have arrived, compare SHA-256 hashes on the respective machines:
```bash
sha256sum input.bin
sha256sum received.bin
```

Matching hashes verify identical file contents. The DONE acknowledgment and output-file size alone do not establish correctness.

## Congestion-control experiments
The applications inherit the system's selected TCP congestion-control algorithm when they open sockets. They do not select BBR themselves.

### BBR
On each test endpoint, record the current algorithm and check availability:
```bash
sysctl net.ipv4.tcp_congestion_control
sysctl net.ipv4.tcp_available_congestion_control
```

If BBR is not listed and the kernel provides it as a module:
```bash
sudo modprobe tcp_bbr
```

Select BBR before starting a new transfer:
```bash
sudo sysctl -w net.ipv4.tcp_congestion_control=bbr
```

Restore the previous setting after the experiment. These commands change the system default for new TCP connections.

### Custom RTT-based module
`lab3_rttdiff.c` is a separate experimental algorithm; it is **not an implementation of BBR**.

It compares smoothed RTT against a tracked minimum RTT when calculating the slow-start threshold after a loss event. With low RTT inflation, it applies a gentle reduction; with higher inflation, it applies a multiplicative reduction. Its congestion-avoidance callback uses Linux TCP slow-start and additive-increase helpers.

| Parameter | Default | Meaning |
| --- | --- | --- |
| `infl_thresh_permille` | `150` | RTT-inflation threshold of 15%. |
| `beta_permille` | `700` | Multiplicative factor of 0.7. |
| `min_cwnd_pkts` | `4` | Minimum window used by the module's calculations, in packets. |

The supplied archive does not contain a Makefile. To build the module, create a file named `Makefile` beside `lab3_rttdiff.c` with the following content. Recipe lines must begin with a **tab**:

```makefile
obj-m += lab3_rttdiff.o
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
```

Build and load it on a compatible test system:
```bash
sudo apt-get install -y linux-headers-$(uname -r)
make
sudo insmod lab3_rttdiff.ko
sysctl net.ipv4.tcp_available_congestion_control
sudo sysctl -w net.ipv4.tcp_congestion_control=lab3_rttdiff
```

Linux internal TCP APIs vary by kernel version; compilation or adaptation may be necessary. Module signing policies may also affect loading. Restore the previous congestion-control default and close connections using this algorithm before unloading:

```bash
sudo rmmod lab3_rttdiff
```

## Laboratory experiments and reported result
The accompanying lab report describes AWS VPC/EC2 networking, VyOS routing, bandwidth limiting, and delay/loss experiments.

It reports **55.83 Mbps** for a BBR-based experiment with a nominal **100 Mbps** link, **200 ms RTT**, and **20% packet loss configured in both directions**. This is a historical laboratory observation, not a benchmark independently reproduced for this repository. The custom RTT-based module should not be credited with that BBR result.

The original notes identify 64 client threads as a useful setting for the tested loss scenario. The best thread count depends on the environment and should be measured rather than assumed.

## Current limitations
- The DONE acknowledgment is sent without checking that every server worker has finished writing.
- Server shutdown can remain blocked in `accept()` and may require manual termination with `Ctrl+C`.
- Socket error handling, partial header sends, short disk writes, and resource cleanup need improvement.

## Contributors
- Jacinta Rose Menezes
- Yichen Zheng
- Junjun Liu

Developed for USC EE 542 Laboratory 3: AWS Tutorial and TCP Congestion Control Modification.
