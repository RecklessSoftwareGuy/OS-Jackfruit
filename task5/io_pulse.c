/*
 * io_pulse.c — I/O-Bound Workload for Scheduler Experiments (Task 5)
 *
 * PURPOSE:
 *   This program simulates an I/O-bound process. It alternates between
 *   short bursts of file I/O (write + fsync) and voluntary sleeps,
 *   causing the process to frequently yield the CPU. This makes it
 *   ideal for comparing against a CPU-bound workload: the Linux CFS
 *   scheduler treats I/O-bound processes differently because they
 *   voluntarily sleep and accumulate less "virtual runtime."
 *
 * HOW IT WORKS:
 *   1. Opens a temporary output file for writing.
 *   2. For each iteration: writes a small line, calls fsync() to force
 *      the data to disk (creating a real I/O wait), then sleeps.
 *   3. Prints a progress report after each iteration.
 *   4. On exit, prints a summary of total iterations and elapsed time.
 *
 * USAGE:
 *   ./io_pulse [iterations] [sleep_ms]
 *
 * EXAMPLE:
 *   ./io_pulse 20 200    # 20 write iterations, 200ms sleep between each
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_OUTPUT "/tmp/io_pulse.out"

static unsigned int parse_uint(const char *arg, unsigned int fallback)
{
    char *end = NULL;
    unsigned long value = strtoul(arg, &end, 10);

    if (!arg || *arg == '\0' || (end && *end != '\0') || value == 0)
        return fallback;
    return (unsigned int)value;
}

int main(int argc, char *argv[])
{
    const unsigned int iterations = (argc > 1) ? parse_uint(argv[1], 20) : 20;
    const unsigned int sleep_ms   = (argc > 2) ? parse_uint(argv[2], 200) : 200;
    int fd;
    unsigned int i;
    time_t start, end_time;

    printf("========================================\n");
    printf("  [IO_PULSE] STARTING I/O-BOUND WORKLOAD\n");
    printf("========================================\n");
    printf("  PID           : %d\n", getpid());
    printf("  Iterations    : %u\n", iterations);
    printf("  Sleep interval: %u ms (between I/O bursts)\n", sleep_ms);
    printf("  Behaviour     : Write burst → fsync → sleep (yields CPU voluntarily)\n");
    printf("  Purpose       : Demonstrate I/O-bound scheduling vs CPU-bound\n");
    printf("========================================\n\n");
    fflush(stdout);

    fd = open(DEFAULT_OUTPUT, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    start = time(NULL);

    for (i = 0; i < iterations; i++) {
        char line[128];
        int len = snprintf(line, sizeof(line), "io_pulse iteration=%u\n", i + 1);

        if (write(fd, line, (size_t)len) != len) {
            perror("write");
            close(fd);
            return 1;
        }

        fsync(fd);
        printf("  [IO_PULSE] Completed burst #%u/%u  (wrote %d bytes, flushed to disk)\n",
               i + 1, iterations, len);
        fflush(stdout);
        usleep(sleep_ms * 1000U);
    }

    end_time = time(NULL);
    close(fd);

    printf("\n========================================\n");
    printf("  [IO_PULSE] WORKLOAD COMPLETE\n");
    printf("========================================\n");
    printf("  Total iterations  : %u\n", iterations);
    printf("  Total elapsed time: %ld seconds\n", (long)(end_time - start));
    printf("  Output file       : %s\n", DEFAULT_OUTPUT);
    printf("  Note: This process spent most of its time sleeping (I/O wait),\n");
    printf("        freeing the CPU for other processes to use.\n");
    printf("========================================\n");
    fflush(stdout);

    return 0;
}
