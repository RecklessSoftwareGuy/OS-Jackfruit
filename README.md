# Multi-Container Linux Runtime — Task 5: Scheduler Experiments

**Branch:** `assigned`  
**Task:** 5 — Scheduler Experiments and Analysis

---

## Overview

This branch implements **Task 5** of the Multi-Container Runtime project. It contains a self-contained suite of scheduler experiments that demonstrate how the **Linux CFS (Completely Fair Scheduler)** handles different workloads and priority configurations.

Instead of running workloads inside containers (which requires the full engine/supervisor from Tasks 1–4), this implementation launches processes directly using `fork()` and applies `nice()` values to control scheduling priority. This isolates the scheduler behaviour cleanly and makes the experiments reproducible on any Ubuntu VM.

## Repository Structure

```
├── Makefile                  # Root build — delegates to task5/
├── README.md                 # This file
├── instructions.md           # Step-by-step execution guide (OS Scholar)
└── task5/
    ├── Makefile              # Builds experiment, cpu_hog, io_pulse
    ├── experiment.c          # Main experiment harness (runs both experiments)
    ├── cpu_hog.c             # CPU-bound workload (tight arithmetic loop)
    ├── io_pulse.c            # I/O-bound workload (write burst + sleep)
    └── run_experiments.sh    # One-command wrapper to build and run everything
```

## What Task 5 Demonstrates

### Experiment 1: Priority Impact on CPU-Bound Processes

Two identical `cpu_hog` processes run simultaneously for 8 seconds each:
- **Process A** runs at `nice=0` (default priority, CFS weight ≈ 1024)
- **Process B** runs at `nice=10` (lower priority, CFS weight ≈ 110)

**Expected Result:** Process A completes significantly more iterations per second than Process B because CFS allocates CPU time proportionally to process weight.

### Experiment 2: CPU-Bound vs I/O-Bound Scheduling

A `cpu_hog` (CPU-bound, 8 seconds) and an `io_pulse` (I/O-bound, 15 iterations × 200ms sleep) run concurrently at the same nice value:

**Expected Result:**
- `io_pulse` finishes first (~3 seconds) because it voluntarily yields the CPU during sleeps
- `cpu_hog` takes its full 8 seconds but gets nearly all available CPU time
- Neither process starves the other — CFS is both fair and responsive

## Build & Run Instructions

### Prerequisites

Ubuntu 22.04 or 24.04 VM with build-essential:

```bash
sudo apt update
sudo apt install -y build-essential
```

### Build

```bash
make          # Build from project root
# OR
cd task5 && make all
```

### Run the Experiments

```bash
cd task5
sudo ./experiment
```

Or use the convenience wrapper:

```bash
cd task5
chmod +x run_experiments.sh
sudo ./run_experiments.sh
```

The wrapper saves output to `experiment_output.log` for report screenshots.

### Run Individual Workloads

```bash
cd task5
./cpu_hog 10          # CPU-bound for 10 seconds
./io_pulse 20 200     # 20 I/O iterations, 200ms sleep between each
```

## Engineering Analysis

### 5. Scheduling Behaviour

The Linux CFS (Completely Fair Scheduler) maintains a virtual runtime (`vruntime`) for each process. The key observations from our experiments:

**Priority and CPU Weight (Experiment 1):**
- CFS assigns a "weight" to each process based on its nice value. The weight table maps nice=0 to weight ≈ 1024 and nice=10 to weight ≈ 110.
- When two processes compete for the same CPU, CFS distributes time proportionally: the nice=0 process gets ~90% of CPU time; the nice=10 process gets ~10%.
- This is proportional fair scheduling, not strict preemptive priority. The low-priority process still runs — it just gets less time.

**I/O-Bound vs CPU-Bound (Experiment 2):**
- I/O-bound processes voluntarily sleep (e.g., `usleep()` between disk writes). During sleep, their `vruntime` does not advance.
- When an I/O-bound process wakes up, its `vruntime` is relatively low, so CFS picks it up immediately — this is the "sleeper fairness" heuristic that gives I/O-bound processes high responsiveness.
- The CPU-bound process's `vruntime` advances continuously, but it still gets scheduled fairly based on its weight. It simply never benefits from sleeper fairness.
- **Net effect:** I/O processes feel responsive; CPU processes get throughput. Neither starves.

### Design Decisions & Tradeoffs

| Decision | Rationale | Tradeoff |
|----------|-----------|----------|
| Standalone fork + nice | Clean isolation of scheduler behaviour without container overhead | Does not exercise the full engine/supervisor pipeline |
| Wall-clock timing via `clock_gettime(CLOCK_MONOTONIC)` | High-resolution, monotonic, unaffected by NTP drift | Does not measure per-process CPU time directly |
| Inline analysis in output | Teacher sees theory alongside results | Longer terminal output |
| 8-second experiment duration | Long enough for reliable measurement, short enough for demos | May show variance on heavily loaded VMs |

## Scheduler Experiment Results

Results are printed inline by `./experiment`. Key metrics to compare:

| Metric | Experiment 1 (nice=0 vs nice=10) | Experiment 2 (CPU vs I/O) |
|--------|----------------------------------|---------------------------|
| **Iterations/sec** | Higher for nice=0 process | N/A (different workload types) |
| **Wall-clock time** | Both ~8s (same requested duration) | I/O: ~3s; CPU: ~8s |
| **Finish order** | Simultaneous (same duration) | I/O finishes first |
| **CPU share** | ~90% / ~10% | CPU gets nearly 100% while I/O sleeps |
