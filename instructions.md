# Task 5: Scheduler Experiments — Setup & Execution Guide

> **Author:** OS Scholar  
> **Persona:** OS Scholar & Teacher (from `.agents/agents.md`)  
> **Scope:** Step-by-step commands to build, run, and capture output from Task 5

---

## Prerequisites

You need an **Ubuntu 22.04 or 24.04** virtual machine (not WSL). Secure Boot does not need to be disabled for Task 5 since we are not loading any kernel modules.

### Step 1 — Install Build Dependencies

Open a terminal and run:

```bash
sudo apt update
sudo apt install -y build-essential
```

This installs `gcc`, `make`, and standard C headers.

---

## Building the Experiments

### Step 2 — Navigate to the Project Root

```bash
cd ~/Projects/OS-Jackfruit
```

*(Adjust the path if your repository is cloned elsewhere.)*

### Step 3 — Switch to the `assigned` Branch

```bash
git checkout assigned
```

### Step 4 — Build Everything

From the project root:

```bash
make
```

Or navigate directly into `task5/`:

```bash
cd task5
make all
```

**Expected output:**

```
gcc -O2 -Wall -Wextra -o experiment experiment.c
gcc -O2 -Wall -Wextra -o cpu_hog cpu_hog.c
gcc -O2 -Wall -Wextra -o io_pulse io_pulse.c
```

Three binaries should now exist in `task5/`:

```bash
ls -la experiment cpu_hog io_pulse
```

---

## Running the Experiments

### Step 5 — Run the Full Experiment Suite

The experiment harness must be run with `sudo` because setting negative nice values (high priority) requires root privileges.

```bash
cd task5
sudo ./experiment
```

This runs two experiments back-to-back (~20 seconds total):

1. **Experiment 1** — Two CPU-bound processes with different nice values (nice=0 vs nice=10)
2. **Experiment 2** — CPU-bound process vs I/O-bound process at the same nice value

### Alternative: Use the Wrapper Script

```bash
cd task5
chmod +x run_experiments.sh
sudo ./run_experiments.sh
```

The wrapper script builds, runs, and saves all output to `experiment_output.log`.

---

## What to Look For in the Output

### Experiment 1 Output

Look for lines like:

```
  [CPU_HOG] Progress | Elapsed: 1/8 sec | Iterations so far: XXXXX
```

The process with **nice=0** (high priority) should report a **much higher iteration count per second** than the process with **nice=10** (low priority). This is because the Linux CFS scheduler gives the nice=0 process approximately 9× more CPU time.

At the end of the experiment, each `cpu_hog` prints its **Iterations/second**. Compare these two numbers — the disparity demonstrates CFS proportional fair scheduling.

### Experiment 2 Output

Look for:
- **io_pulse** finishes in ~3 seconds (it sleeps between I/O bursts)
- **cpu_hog** finishes in ~8 seconds (it uses CPU the entire time)
- The **FINISH ORDER** section confirms io_pulse exits first

The **ANALYSIS** section in the output explains why: I/O-bound processes accumulate less virtual runtime (vruntime) because they sleep, so CFS schedules them immediately when they wake up.

---

## Running Individual Workloads (Optional)

You can also run each workload standalone to observe its behaviour in isolation:

### CPU-Bound Workload

```bash
cd task5
./cpu_hog 10
```

Runs a tight arithmetic loop for 10 seconds, printing progress each second.

### I/O-Bound Workload

```bash
cd task5
./io_pulse 20 200
```

Performs 20 write-then-sleep iterations with 200ms between each burst.

---

## Capturing Output for Your Report

### Option A: Screenshot the Terminal

Run the experiment and take a screenshot of the terminal output showing:
- The iteration counts from Experiment 1 (different values for each process)
- The wall-clock times and finish order from Experiment 2
- The ANALYSIS sections that explain the CFS behaviour

### Option B: Use the Log File

```bash
cd task5
sudo ./run_experiments.sh
cat experiment_output.log
```

The log file contains the complete output and can be included in your report.

---

## Cleaning Up

```bash
cd task5
make clean
```

This removes compiled binaries and log files.

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `Permission denied` when running experiment | Use `sudo ./experiment` — setting nice values requires root |
| `make: gcc: No such file or directory` | Run `sudo apt install -y build-essential` |
| Iteration counts are nearly equal for both processes | Ensure you're running with `sudo` so nice values are applied correctly. Also try on a VM with fewer cores (1-2 CPUs) for more visible contention |
| Output scrolls too fast | Use `sudo ./run_experiments.sh` to save output to a log file |

---

## Summary of Commands (Quick Reference)

```bash
# 1. Install dependencies
sudo apt update && sudo apt install -y build-essential

# 2. Navigate and build
cd ~/Projects/OS-Jackfruit
git checkout assigned
cd task5
make all

# 3. Run experiments
sudo ./experiment

# 4. (Optional) Save output to log
chmod +x run_experiments.sh
sudo ./run_experiments.sh
cat experiment_output.log

# 5. Clean up
make clean
```
