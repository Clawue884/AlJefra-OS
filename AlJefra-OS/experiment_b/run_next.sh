#!/bin/bash
# Auto-chain: wait for current run to finish, then start new run with security analysis
cd /home/sidra/aljefra_os_ai/os_ai/AlJefra-OS/experiment_b

echo "[$(date)] Waiting for current evolution (PID 138374) to finish..."
while kill -0 138374 2>/dev/null; do
    sleep 60
done
echo "[$(date)] Current run finished."
echo ""

# Run new binary on all major components sequentially
for comp in kernel network storage smp memory bus io syscalls timer; do
    echo "=============================================="
    echo "[$(date)] Starting: $comp (with ratio allocation + security analysis)"
    echo "=============================================="
    ./evolve_bin "$comp" 999 2>&1 | tee "results/run_${comp}_$(date +%Y%m%d_%H%M%S).log"
    echo "[$(date)] Finished: $comp"
    echo ""
done

echo "[$(date)] ALL COMPONENTS COMPLETE"
