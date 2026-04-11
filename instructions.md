# OS Scholar's Comprehensive Guide: The Multi-Container Runtime Curriculum

Greetings, scholar. I have expanded this guide to cover the full implementation of all six tasks defined in the OS-Jackfruit project. 

In this curriculum, we move beyond simple cleanup and explore the evolution of our container runtime—from namespace isolation to kernel-level memory enforcement and scheduling analysis. We will use the final, cumulative implementation in `task6` to demonstrate all features.

---

### Phase 0: The Laboratory Foundation
Before we begin our experiments, we must establish the environment and the base filesystem.

```bash
# Update system and install dependencies
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) wget

# Navigate to the final implementation directory
cd task6

# Establish the Alpine Rootfs Template
mkdir -p rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base

# Prepare writable copies for our containers
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
cp -a ./rootfs-base ./rootfs-gamma

# Compile the final system
make clean
make
```

---

### Phase 1: Isolation and Supervision (Task 1)
**Objective**: Demonstrate PID, UTS, and mount namespace isolation.

```bash
# Start the supervisor (Terminal 1)
sudo ./engine supervisor ./rootfs-base

# In Terminal 2, launch a container with a unique hostname and shell
sudo ./engine start alpha ./rootfs-alpha /bin/sh

# Verify Isolation: Check that the container sees its own PID 1
sudo ./engine logs alpha | grep "ps" # Or interact with it if 'run' was used
```

---

### Phase 2: CLI Operations and Control Path (Task 2)
**Objective**: Interact with the supervisor via the UNIX domain socket IPC (Path B).

```bash
# List all active and tracked containers
sudo ./engine ps

# Start another container to demonstrate multi-tenancy
sudo ./engine start beta ./rootfs-beta /bin/sh

# Observe the second entry in the metadata list
sudo ./engine ps
```

---

### Phase 3: Bounded-Buffer Logging (Task 3)
**Objective**: Verify that container output is captured via pipes into a synchronized user-space buffer (Path A).

```bash
# Run a command that generates output inside the container
# (Assuming you have a tool or can echo to a log)
sudo ./engine start gamma ./rootfs-gamma /bin/echo "Scholarship is the key to mastery"

# Inspect the persistent log file managed by the supervisor's consumer threads
sudo ./engine logs gamma
```

---

### Phase 4: Kernel Memory Monitoring (Task 4)
**Objective**: Use `ioctl` to register PIDs and enforce soft/hard memory limits.

```bash
# Load the Kernel Module
sudo insmod monitor.ko

# Copy the 'memory_hog' tool into the container's rootfs
cp memory_hog ./rootfs-alpha/bin/

# Launch container with explicit memory constraints:
# Soft limit: 20MB, Hard limit: 40MB
sudo ./engine start alpha ./rootfs-alpha /bin/memory_hog 50 --soft-mib 20 --hard-mib 40

# Monitor dmesg for soft-limit warnings and hard-limit SIGKILL signals
dmesg | tail -f
```

---

### Phase 5: Scheduler Experiments (Task 5)
**Objective**: Analyze Linux scheduling behavior using `nice` values and diverse workloads.

```bash
# Launch a CPU-bound process with high priority (low nice)
sudo ./engine start cpu_high ./rootfs-alpha /bin/cpu_hog --nice -10

# Launch another CPU-bound process with low priority (high nice)
sudo ./engine start cpu_low ./rootfs-beta /bin/cpu_hog --nice 19

# Launch an I/O-bound process to observe preemption behavior
sudo ./engine start io_task ./rootfs-alpha /bin/io_pulse

# Analyze the completion times or CPU shares in the logs
sudo ./engine logs cpu_high
sudo ./engine logs cpu_low
```

---

### Phase 6: Resource Reclamation (Task 6)
**Objective**: Ensure the system leaves no trace upon completion.

```bash
# Stop all running experiments
sudo ./engine stop alpha
sudo ./engine stop beta
sudo ./engine stop cpu_high
sudo ./engine stop cpu_low
sudo ./engine stop io_task

# Shutdown the supervisor (CTRL+C in Terminal 1)
# Observe the orderly thread joins and signal handling

# Unload the Kernel Module
sudo rmmod monitor

# Final Audit: Ensure no leaking zombies or sockets
ps aux | grep engine
ls /tmp/mini_runtime.sock
```

---

### Academic Summary
By executing these phases, you have traversed the entire landscape of modern containerization:
- **Task 1-3** established the user-space orchestration and IPC.
- **Task 4** bridged the gap between user-space intent and kernel-space enforcement.
- **Task 5** provided empirical evidence of OS scheduling heuristics.
- **Task 6** upheld the engineering standards of resource discipline.

Do you wish to dive deeper into the `ioctl` implementation details or the synchronization primitives used in the bounded buffer? I remain at your service.
