# ============================================================
# Task 5 Makefile — Scheduler Experiments Only
#
# This builds the standalone scheduler experiment suite.
# No kernel module or runtime engine required for Task 5.
# ============================================================

CC := gcc
CFLAGS := -O2 -Wall -Wextra

USER_TARGETS := experiment cpu_hog io_pulse

all: $(USER_TARGETS)

ci: $(USER_TARGETS)

experiment: experiment.c
	$(CC) $(CFLAGS) -o experiment experiment.c

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o cpu_hog cpu_hog.c

io_pulse: io_pulse.c
	$(CC) $(CFLAGS) -o io_pulse io_pulse.c

clean:
	rm -f $(USER_TARGETS)
	rm -f *.log
	rm -f /tmp/io_pulse.out

.PHONY: all ci clean
