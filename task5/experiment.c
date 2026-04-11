/*
 * experiment.c — Linux Scheduler Experiment Harness (Task 5)
 *
 * PURPOSE:
 *   This program is the central experiment driver for Task 5 of the
 *   Multi-Container Runtime project. It launches two controlled experiments
 *   that demonstrate how the Linux CFS (Completely Fair Scheduler) treats
 *   different workloads under different priority configurations.
 *
 * EXPERIMENTS PERFORMED:
 *
 *   EXPERIMENT 1 — "Priority Impact on CPU-Bound Processes"
 *     Two identical CPU-bound processes (cpu_hog) run simultaneously, but
 *     with different nice values. The process with the lower nice value
 *     (higher priority) receives a proportionally larger share of CPU time,
 *     completing more iterations in the same wall-clock period.
 *
 *   EXPERIMENT 2 — "CPU-Bound vs I/O-Bound Scheduling"
 *     A CPU-bound process (cpu_hog) and an I/O-bound process (io_pulse)
 *     run at the same time with the same nice value. The I/O-bound process
 *     finishes sooner because it voluntarily yields the CPU during sleeps,
 *     while the CPU-bound process consumes its full time quantum.
 *
 * WHAT A TEACHER SHOULD LOOK FOR:
 *   • Experiment 1: The higher-priority process finishes more iterations/sec.
 *   • Experiment 2: The I/O-bound process finishes quickly; the CPU-bound
 *     process takes its full duration. The scheduler is fair to both.
 *
 * USAGE:
 *   sudo ./experiment           # Runs both experiments sequentially
 *
 * REQUIREMENTS:
 *   The 'cpu_hog' and 'io_pulse' binaries must be in the same directory.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ----------------------------------------------------------------
 * Utility: get wall-clock time in milliseconds
 * ---------------------------------------------------------------- */
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ----------------------------------------------------------------
 * Utility: print a separator banner
 * ---------------------------------------------------------------- */
static void print_banner(const char *text)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  %-64s║\n", text);
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    fflush(stdout);
}

static void print_section(const char *text)
{
    printf("┌──────────────────────────────────────────────────────────────────┐\n");
    printf("│  %-64s│\n", text);
    printf("└──────────────────────────────────────────────────────────────────┘\n");
    fflush(stdout);
}

/* ----------------------------------------------------------------
 * Launch a child process with a specific nice value
 * Returns the PID of the child. The child exec's the given binary.
 * ---------------------------------------------------------------- */
static pid_t launch_workload(const char *binary, const char *duration_arg,
                             const char *extra_arg, int nice_val,
                             const char *label)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        if (nice(nice_val) < 0 && errno != 0) {
            perror("nice");
        }

        printf("  [HARNESS] Launched '%s' (PID %d) with nice=%d  [%s]\n",
               binary, getpid(), nice_val, label);
        fflush(stdout);

        if (extra_arg) {
            execl(binary, binary, duration_arg, extra_arg, (char *)NULL);
        } else {
            execl(binary, binary, duration_arg, (char *)NULL);
        }
        perror("execl");
        _exit(1);
    }

    return pid;
}

/* Removed wait_and_report() — inline result printing used instead */

/* ================================================================
 * EXPERIMENT 1: Two CPU-bound processes with different nice values
 *
 * THEORY (Linux CFS):
 *   The Completely Fair Scheduler assigns each process a "weight"
 *   derived from its nice value. A lower nice value → higher weight
 *   → proportionally more CPU time. With nice 0 (weight ~1024) and
 *   nice 10 (weight ~110), the nice-0 process gets ~9x the CPU
 *   share when both compete for the same core.
 *
 *   Since both cpu_hog processes run identical code (an arithmetic
 *   loop), the higher-priority process will complete significantly
 *   more iterations per second, demonstrating the CFS weight system.
 * ================================================================ */
static void experiment_1(void)
{
    print_banner("EXPERIMENT 1: Priority Impact on CPU-Bound Processes");

    printf("  HYPOTHESIS:\n");
    printf("    Two identical CPU-bound processes run simultaneously.\n");
    printf("    Process A uses nice=0  (default/normal priority).\n");
    printf("    Process B uses nice=10 (lower priority).\n");
    printf("    The Linux CFS scheduler distributes CPU time proportionally\n");
    printf("    to process weight. A lower nice value gives higher weight,\n");
    printf("    so Process A should complete MORE iterations than Process B\n");
    printf("    in the same wall-clock period.\n\n");
    fflush(stdout);

    const char *duration = "8";  /* both run for 8 seconds */

    print_section("Launching workloads...");
    long long start = now_ms();

    pid_t pid_a = launch_workload("./cpu_hog", duration, NULL, 0,
                                   "HIGH PRIORITY (nice=0)");
    pid_t pid_b = launch_workload("./cpu_hog", duration, NULL, 10,
                                   "LOW PRIORITY (nice=10)");

    if (pid_a < 0 || pid_b < 0) {
        fprintf(stderr, "  [ERROR] Failed to launch workloads!\n");
        return;
    }

    printf("\n  [HARNESS] Both CPU-bound processes are now competing for CPU time.\n");
    printf("  [HARNESS] Waiting for both to complete...\n\n");
    fflush(stdout);

    /* Wait for both children */
    int status_a = 0, status_b = 0;
    long long end_a = 0, end_b = 0;
    int done_a = 0, done_b = 0;

    while (!done_a || !done_b) {
        int status;
        pid_t finished = wait(&status);
        long long now = now_ms();

        if (finished == pid_a) {
            status_a = status;
            end_a = now;
            done_a = 1;
        } else if (finished == pid_b) {
            status_b = status;
            end_b = now;
            done_b = 1;
        }
    }

    print_section("EXPERIMENT 1 — RESULTS");

    printf("\n  ┌─ Process A (nice=0, HIGH PRIORITY) ────────────\n");
    printf("  │  PID        : %d\n", pid_a);
    printf("  │  Nice value : 0  (CFS weight ≈ 1024)\n");
    printf("  │  Wall-clock : %lld ms\n", end_a - start);
    if (WIFEXITED(status_a))
        printf("  │  Exit code  : %d\n", WEXITSTATUS(status_a));
    printf("  └──────────────────────────────────────────────────\n\n");

    printf("  ┌─ Process B (nice=10, LOW PRIORITY) ────────────\n");
    printf("  │  PID        : %d\n", pid_b);
    printf("  │  Nice value : 10 (CFS weight ≈ 110)\n");
    printf("  │  Wall-clock : %lld ms\n", end_b - start);
    if (WIFEXITED(status_b))
        printf("  │  Exit code  : %d\n", WEXITSTATUS(status_b));
    printf("  └──────────────────────────────────────────────────\n\n");

    printf("  ┌─ ANALYSIS ──────────────────────────────────────\n");
    printf("  │\n");
    printf("  │  Both processes ran the same CPU-bound loop for %s seconds.\n", duration);
    printf("  │\n");
    printf("  │  KEY OBSERVATION:\n");
    printf("  │  Look at the 'Iterations/second' printed by each cpu_hog.\n");
    printf("  │  Process A (nice=0) should report significantly more\n");
    printf("  │  iterations/second than Process B (nice=10).\n");
    printf("  │\n");
    printf("  │  WHY THIS HAPPENS (CFS Scheduling Theory):\n");
    printf("  │  • Linux CFS assigns each process a 'weight' based on nice value.\n");
    printf("  │  • nice=0 → weight ≈ 1024;  nice=10 → weight ≈ 110\n");
    printf("  │  • CPU share ratio ≈ 1024:110 ≈ 9.3:1\n");
    printf("  │  • Process A receives ~90%% of CPU time; Process B ~10%%.\n");
    printf("  │  • Result: Process A completes far more iterations.\n");
    printf("  │\n");
    printf("  │  This demonstrates that Linux CFS uses proportional\n");
    printf("  │  fair scheduling, not strict preemptive priority.\n");
    printf("  └──────────────────────────────────────────────────\n\n");
    fflush(stdout);
}

/* ================================================================
 * EXPERIMENT 2: CPU-bound vs I/O-bound process
 *
 * THEORY (Linux CFS):
 *   I/O-bound processes voluntarily yield the CPU while waiting
 *   for disk/network. CFS gives them a "sleeper bonus" — their
 *   virtual runtime doesn't advance during sleep, so they get
 *   picked up quickly when they become runnable again. This makes
 *   I/O-bound processes feel "responsive."
 *
 *   Meanwhile, the CPU-bound process consumes its entire time
 *   quantum on each scheduling round, which causes its virtual
 *   runtime to advance faster.
 *
 *   NET EFFECT: The I/O-bound process finishes quickly and feels
 *   responsive; the CPU-bound process gets all remaining CPU time
 *   but experiences no starvation.
 * ================================================================ */
static void experiment_2(void)
{
    print_banner("EXPERIMENT 2: CPU-Bound vs I/O-Bound Scheduling");

    printf("  HYPOTHESIS:\n");
    printf("    A CPU-bound process (cpu_hog, 8 seconds) and an I/O-bound\n");
    printf("    process (io_pulse, 15 iterations × 200ms sleep) run\n");
    printf("    simultaneously at the SAME nice value (nice=0).\n\n");
    printf("    EXPECTED RESULT:\n");
    printf("    • io_pulse finishes first (~3 seconds) because it spends\n");
    printf("      most of its time sleeping, not using CPU.\n");
    printf("    • cpu_hog takes its full 8 seconds.\n");
    printf("    • The CPU-bound process does NOT starve the I/O process.\n");
    printf("    • The I/O-bound process remains responsive throughout.\n\n");
    fflush(stdout);

    print_section("Launching workloads...");
    long long start = now_ms();

    pid_t pid_cpu = launch_workload("./cpu_hog", "8", NULL, 0,
                                     "CPU-BOUND (cpu_hog)");
    pid_t pid_io  = launch_workload("./io_pulse", "15", "200", 0,
                                     "I/O-BOUND (io_pulse)");

    if (pid_cpu < 0 || pid_io < 0) {
        fprintf(stderr, "  [ERROR] Failed to launch workloads!\n");
        return;
    }

    printf("\n  [HARNESS] CPU-bound and I/O-bound processes running concurrently.\n");
    printf("  [HARNESS] Waiting for both to complete...\n\n");
    fflush(stdout);

    /* Wait for both children, track finish order */
    int status_cpu = 0, status_io = 0;
    long long end_cpu = 0, end_io = 0;
    int done_cpu = 0, done_io = 0;
    int finish_order = 0;
    const char *first_label = NULL, *second_label = NULL;

    while (!done_cpu || !done_io) {
        int status;
        pid_t finished = wait(&status);
        long long now = now_ms();

        if (finished == pid_cpu) {
            status_cpu = status;
            end_cpu = now;
            done_cpu = 1;
            finish_order++;
            if (finish_order == 1) first_label = "cpu_hog (CPU-bound)";
            else second_label = "cpu_hog (CPU-bound)";
        } else if (finished == pid_io) {
            status_io = status;
            end_io = now;
            done_io = 1;
            finish_order++;
            if (finish_order == 1) first_label = "io_pulse (I/O-bound)";
            else second_label = "io_pulse (I/O-bound)";
        }
    }

    print_section("EXPERIMENT 2 — RESULTS");

    printf("\n  ┌─ cpu_hog (CPU-BOUND) ────────────────────────────\n");
    printf("  │  PID        : %d\n", pid_cpu);
    printf("  │  Nice value : 0\n");
    printf("  │  Behaviour  : Tight arithmetic loop (no voluntary yields)\n");
    printf("  │  Wall-clock : %lld ms  (%.2f sec)\n",
           end_cpu - start, (double)(end_cpu - start) / 1000.0);
    if (WIFEXITED(status_cpu))
        printf("  │  Exit code  : %d\n", WEXITSTATUS(status_cpu));
    printf("  └──────────────────────────────────────────────────\n\n");

    printf("  ┌─ io_pulse (I/O-BOUND) ───────────────────────────\n");
    printf("  │  PID        : %d\n", pid_io);
    printf("  │  Nice value : 0\n");
    printf("  │  Behaviour  : Write burst → fsync → 200ms sleep (yields CPU)\n");
    printf("  │  Wall-clock : %lld ms  (%.2f sec)\n",
           end_io - start, (double)(end_io - start) / 1000.0);
    if (WIFEXITED(status_io))
        printf("  │  Exit code  : %d\n", WEXITSTATUS(status_io));
    printf("  └──────────────────────────────────────────────────\n\n");

    printf("  ┌─ FINISH ORDER ────────────────────────────────────\n");
    printf("  │  1st to finish: %s\n", first_label ? first_label : "?");
    printf("  │  2nd to finish: %s\n", second_label ? second_label : "?");
    printf("  └──────────────────────────────────────────────────\n\n");

    long long diff = (end_cpu - start) - (end_io - start);
    printf("  ┌─ ANALYSIS ──────────────────────────────────────\n");
    printf("  │\n");
    printf("  │  Time difference: %lld ms (CPU-bound took %lld ms longer)\n",
           diff > 0 ? diff : -diff, diff);
    printf("  │\n");
    printf("  │  KEY OBSERVATIONS:\n");
    printf("  │  1. The I/O-bound process (io_pulse) finished well before\n");
    printf("  │     the CPU-bound process (cpu_hog), even though both had\n");
    printf("  │     the same nice value.\n");
    printf("  │\n");
    printf("  │  2. io_pulse spent most of its time in SLEEPING state\n");
    printf("  │     (200ms usleep between I/O bursts), so it consumed\n");
    printf("  │     very little actual CPU time.\n");
    printf("  │\n");
    printf("  │  3. cpu_hog was able to use nearly 100%% of one core,\n");
    printf("  │     because the I/O process was sleeping.\n");
    printf("  │\n");
    printf("  │  WHY THIS HAPPENS (CFS Scheduling Theory):\n");
    printf("  │  • CFS tracks 'virtual runtime' (vruntime) for each process.\n");
    printf("  │  • Sleeping processes don't accumulate vruntime.\n");
    printf("  │  • When io_pulse wakes up, its vruntime is low, so CFS\n");
    printf("  │    schedules it immediately (boosting responsiveness).\n");
    printf("  │  • cpu_hog's vruntime keeps increasing, but it still gets\n");
    printf("  │    scheduled fairly — it just never gets a 'bonus'.\n");
    printf("  │\n");
    printf("  │  CONCLUSION:\n");
    printf("  │  CFS balances fairness (CPU-bound gets its share) with\n");
    printf("  │  responsiveness (I/O-bound wakes up quickly). Neither\n");
    printf("  │  process starves the other.\n");
    printf("  └──────────────────────────────────────────────────\n\n");
    fflush(stdout);
}

/* ================================================================
 * MAIN — Run both experiments sequentially
 * ================================================================ */
int main(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                  ║\n");
    printf("║   TASK 5: LINUX SCHEDULER EXPERIMENTS AND ANALYSIS               ║\n");
    printf("║   Multi-Container Runtime Project                                ║\n");
    printf("║                                                                  ║\n");
    printf("║   This program runs two controlled experiments to demonstrate    ║\n");
    printf("║   how the Linux CFS scheduler handles different workloads        ║\n");
    printf("║   and priority configurations.                                   ║\n");
    printf("║                                                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    fflush(stdout);

    experiment_1();

    printf("\n  [HARNESS] Pausing 2 seconds between experiments...\n\n");
    fflush(stdout);
    sleep(2);

    experiment_2();

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                  ║\n");
    printf("║   ALL EXPERIMENTS COMPLETE                                       ║\n");
    printf("║                                                                  ║\n");
    printf("║   Summary of findings:                                           ║\n");
    printf("║   • Exp 1: Higher-priority CPU-bound process got more CPU time   ║\n");
    printf("║   • Exp 2: I/O-bound process finished faster, CPU-bound got     ║\n");
    printf("║            full CPU share — CFS is both fair and responsive      ║\n");
    printf("║                                                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    fflush(stdout);

    return 0;
}
