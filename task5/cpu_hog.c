/*
 * cpu_hog.c — CPU-Bound Workload for Scheduler Experiments (Task 5)
 *
 * PURPOSE:
 *   This program simulates a purely CPU-bound process. It performs tight
 *   arithmetic loops that keep the CPU fully saturated with no voluntary
 *   I/O waits, making it ideal for observing how the Linux CFS (Completely
 *   Fair Scheduler) distributes CPU time between competing processes.
 *
 * HOW IT WORKS:
 *   1. Parses the desired run duration from argv[1] (default: 10 seconds).
 *   2. Enters a tight loop (no sleep/IO) performing a linear congruential
 *      accumulator operation until the requested wall-clock duration elapses.
 *   3. Prints a progress report every second so the teacher can see that
 *      the process is actively consuming CPU time.
 *   4. On exit, prints a summary with total iterations completed, which
 *      serves as a proxy for "how much CPU time this process received."
 *
 * USAGE:
 *   ./cpu_hog [seconds]
 *
 * EXAMPLE:
 *   ./cpu_hog 8       # Burns CPU for 8 seconds
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static unsigned int parse_seconds(const char *arg, unsigned int fallback)
{
    char *end = NULL;
    unsigned long value = strtoul(arg, &end, 10);

    if (!arg || *arg == '\0' || (end && *end != '\0') || value == 0)
        return fallback;
    return (unsigned int)value;
}

int main(int argc, char *argv[])
{
    const unsigned int duration = (argc > 1) ? parse_seconds(argv[1], 10) : 10;
    const time_t start = time(NULL);
    time_t last_report = start;
    volatile unsigned long long accumulator = 0;
    unsigned long long total_iterations = 0;

    printf("========================================\n");
    printf("  [CPU_HOG] STARTING CPU-BOUND WORKLOAD\n");
    printf("========================================\n");
    printf("  PID           : %d\n", getpid());
    printf("  Duration      : %u seconds\n", duration);
    printf("  Behaviour     : Tight arithmetic loop (no I/O, no sleep)\n");
    printf("  Purpose       : Saturate one CPU core to measure scheduler fairness\n");
    printf("========================================\n\n");
    fflush(stdout);

    while ((unsigned int)(time(NULL) - start) < duration) {
        /* Linear congruential generator — pure arithmetic, no syscalls */
        accumulator = accumulator * 1664525ULL + 1013904223ULL;
        total_iterations++;

        if (time(NULL) != last_report) {
            last_report = time(NULL);
            unsigned int elapsed = (unsigned int)(last_report - start);
            printf("  [CPU_HOG] Progress | Elapsed: %u/%u sec | Iterations so far: %llu\n",
                   elapsed, duration, total_iterations);
            fflush(stdout);
        }
    }

    printf("\n========================================\n");
    printf("  [CPU_HOG] WORKLOAD COMPLETE\n");
    printf("========================================\n");
    printf("  Total wall-clock time : %u seconds\n", duration);
    printf("  Total iterations      : %llu\n", total_iterations);
    printf("  Iterations/second     : %llu\n",
           duration > 0 ? total_iterations / duration : total_iterations);
    printf("========================================\n");
    fflush(stdout);

    return 0;
}
