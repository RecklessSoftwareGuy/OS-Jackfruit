# Multi-Container Linux Runtime

**Team Size:** 2 Students

## Overview
This lightweight Linux container runtime creates isolated user-space processes (using namespaces and pivot_root), supervises them concurrently, and captures logging data efficiently using wait-free IPC pipes into a multithreaded bounded-buffer logging system. On the OS level, it tracks and limits container processes via a Kernel Module.

## Repository Structure

```
├── Makefile
├── task1/ # Multi-Container Runtime with Supervisor
├── task2/ # Supervisor CLI & IPC Signal Handling
├── task3/ # Bounded-Buffer Logging & IPC Streams
├── task4/ # Kernel Memory Monitoring (LKM)
├── task5/ # Experiment Test Suite
├── task6/ # Code identical to task 5; tests cleanup 
└── boilerplate/
```

We chose a completely independent layout for each task level, keeping the `boilerplate` test directory preserved without causing GitHub CI issues.

## Build, Load, and Run

On Ubuntu 24.04/22.04 VM (Secure Boot OFF):

```bash
# Build the top level dependencies
make all

# Navigate to the most mature feature-parity codebase (task6 for ultimate correctness)
cd task6

# Load kernel module
sudo insmod monitor.ko

# Verify control device
ls -l /dev/container_monitor

# Copy the Alpine rootfs for our container
cp -a ../boilerplate/rootfs-base ./rootfs-alpha
cp -a ../boilerplate/rootfs-base ./rootfs-beta

# Start the runtime supervisor
sudo ./engine supervisor ./rootfs-base &

# Launch workloads inside the supervisor
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96

# View tracked containers
sudo ./engine ps

# Inspect logs
sudo ./engine logs alpha

# Wait on a running container (optional feature demonstration)
sudo ./engine run charlie ./rootfs-charlie /bin/sh --soft-mib 50 --hard-mib 100

# Perform gracefully supervised shutdown
sudo ./engine stop alpha
sudo ./engine stop beta

# Inspect kernel logs warnings and kills
dmesg | tail

# Stop the supervisor (pkill or Ctrl+C if foreground via jobs)
sudo pkill -SIGTERM engine

# Unload the module 
sudo rmmod monitor
```

## Engineering Analysis

### 1. Isolation Mechanisms
The runtime ensures robust isolation by leveraging built-in Linux kernel namespaces (`CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS`). A process spawned in `CLONE_NEWPID` sees itself as PID 1, completely isolated from host IDs. We enforce filesystem isolation by bind-mounting the specified container directory to itself, and executing a `pivot_root` which swaps our isolated workspace into the visual `root` `/`. The `/proc` filesystem is mounted explicitly inside the chroot, giving standard system utilities like `ps` correct state mappings. Since we are inside an Alpine environment, only host kernel resources (network queues, VFS, physical CPU registers) are implicitly shared while isolated mappings restrict visibility to them.

### 2. Supervisor and Process Lifecycle
Having a background root-level supervisor removes context-switching and permission bottlenecks from each individual CLI invocation. Our supervisor actively utilizes SIGCHLD non-blocking `waitpid()` reaping in its event loop to eliminate zombie processes immediately upon child exit. Concurrency metadata is stored on the heap safely protected with mutexes, classifying events (like `SIGTERM` on shutdown request vs `SIGKILL` on out-of-bounds LKM hard limit checks) accurately.

### 3. IPC, Threads, and Synchronization
Task synchronization fundamentally uses two completely separated IPC networks per instructions: 
- `Control Path`: A UNIX Domain socket (`AF_UNIX`) accepting synchronous struct payloads `control_request_t` for robust command integration. 
- `Logging Path`: Standard POSIX `pipe()` setup prior to `clone()`, pushing STDOUT/STDERR from children into read-only endpoints monitored by supervisor producer threads.
Once data arrives, it is enqueued into a fixed capacity circular buffer `bounded_buffer_t`. Since it operates with concurrently writing Producers and one Consumer thread, we synchronize access explicitly via `pthread_mutex_t` and dual condition variables `not_empty` and `not_full`. Deadlocks are eliminated by enforcing strict boolean polling on shutdown requests which immediately release conditional wait queues.

### 4. Memory Management and Enforcement
The LKM checks Resident Set Size (`RSS`), tracking active mapped physical memory pages instead of Virtual Paged Space (`VSZ`), accurately reflecting genuine active load rather than mmap reservations. Enforcing soft-limits purely within user-space creates latency due to scheduling drift; kernel mechanisms using `get_rss_bytes` check lists locked on `mutexes` during timer fires, ensuring real-time response latency is virtually non-existent before OOM cascade effects occur. 

### 5. Scheduling Behavior
The runtime accommodates priority scheduling logic by setting `nice` behaviors upon container startup. 
By dispatching two `cpu_hog` workloads, one natively run at standard priority and another initiated with strongly negative `nice` values (--nice -10), the kernel allocates comparatively disproportionate CPU time quotas (CFS). The higher-priority task finishes instructions faster, creating measurable disparities in output latency despite identically bound tasks, proving Linux leverages proportional runtime weights fairly during heavy localized contention.

## Design Decisions & Tradeoffs

1. **Directories vs Single Implementation Repo**: A structured separation of directories (`task1` - `task6`) guarantees clean GitHub action traces at the cost of duplicate footprint templates.
2. **Global Waitpid loops instead of signal handler callbacks**: Keeping our signal handler empty avoids complicated `async-signal-safe` violations and correctly serializes IPC control path instructions in one loop.
3. **Pipes over shared buffers**: Standard pipes gracefully terminate EOF signals for Logging Consumer shutdown immediately on process death with no extra user-space implementation necessary.
4. **Memory Enforcement timer cycle**: A 1-second RSS poll is relatively coarse but avoids high system interrupt loads associated with microsecond granular checking.

## Experiments & Teardown

- **Soft Limit Notification:** Visible inside `dmesg` for lightweight overreaches. 
- **Hard Limit Execution:** `dmesg` accurately reports kill execution (`SIGKILL`), with `ps` command distinguishing states.
- **Cleanup:** `mutex` cleanup on `rmmod` enforces zero kernel module linked-list struct leakage. User-space supervisor employs explicit `bounded_buffer_begin_shutdown()` procedures to safely close threads exactly.
