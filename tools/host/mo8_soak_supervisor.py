#!/usr/bin/env python3
"""Preserve process exit status; record a real tick hang instead of waiting forever."""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

root = Path(sys.argv[1])
command = sys.argv[2:]
log = root / 'run.log'
started = time.time()
with (root / 'invocation.json').open('w') as out:
    json.dump({'argv': command, 'started': started,
               'trace_env': {k: v for k, v in os.environ.items() if k.startswith('IMPACTO_')}}, out, indent=2)
with log.open('wb') as output:
    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    def watchdog():
        previous = -1
        changed = time.monotonic()
        while process.poll() is None:
            size = log.stat().st_size
            if size != previous:
                previous = size
                changed = time.monotonic()
            if time.monotonic() - changed > 180:
                with (root / 'watchdog-blocker.txt').open('w') as report:
                    report.write(f'No log progress for 180 seconds; pid={process.pid}\n')
                    try:
                        subprocess.run(['gdb', '-batch', '-ex', 'set pagination off',
                                        '-ex', 'thread apply all bt', '-p', str(process.pid)],
                                       stdout=report, stderr=subprocess.STDOUT, timeout=30)
                    except (OSError, subprocess.TimeoutExpired) as exc:
                        report.write(str(exc))
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                return
            time.sleep(5)
    watcher = threading.Thread(target=watchdog, daemon=True)
    watcher.start()
    status = process.wait()
(root / 'exit.json').write_text(json.dumps({'exit_code': status, 'elapsed_seconds': time.time() - started}, indent=2) + '\n')
print(f'soak finished: exit={status} evidence={root}', flush=True)
sys.exit(0 if status == 0 else 1)
