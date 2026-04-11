#!/bin/bash
# ============================================================
#  run_experiments.sh — Task 5: Scheduler Experiment Runner
#
#  This script builds and runs the scheduler experiments.
#  It must be run with sudo because nice values below 0
#  require root privileges.
#
#  Usage:
#    chmod +x run_experiments.sh
#    sudo ./run_experiments.sh
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "============================================================"
echo "  Task 5 — Linux Scheduler Experiments"
echo "  Building all executables..."
echo "============================================================"
echo ""

# Build everything
make clean 2>/dev/null || true
make all

echo ""
echo "============================================================"
echo "  Build complete. Starting experiments..."
echo "  (This will take approximately 20 seconds)"
echo "============================================================"
echo ""

# Run the experiment harness
./experiment 2>&1 | tee experiment_output.log

echo ""
echo "============================================================"
echo "  Experiment output has been saved to:"
echo "    $SCRIPT_DIR/experiment_output.log"
echo ""
echo "  You can review the output at any time with:"
echo "    cat $SCRIPT_DIR/experiment_output.log"
echo "============================================================"
echo ""
