#!/usr/bin/env python3
"""Diagnostic: run single TUI test with TRACE logging."""
import sys, os, time, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'test', 'e2e'))
from conftest import MockServer, TuiDriver, A0_BIN, SKILLS_DIR, find_free_port

FIXTURES_DIR = os.path.join(os.path.dirname(__file__), '..', 'test', 'e2e', 'fixtures')
scenario = os.path.join(FIXTURES_DIR, 'streaming_tool_calls.json')

port = find_free_port()
ms = subprocess.Popen(
    [sys.executable, os.path.join(os.path.dirname(__file__), '..', 'test', 'e2e', 'mock_deepseek_server.py'),
     str(port), '--scenario', scenario, '--stream'],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
)
time.sleep(1)

a0_dir = f'/tmp/a0-diag-{os.getpid()}'
os.makedirs(a0_dir, exist_ok=True)

driver = TuiDriver(mock_server=None, a0_dir=a0_dir,
    extra_args=['--mock-api', f'http://127.0.0.1:{port}', '--no-b1'],
    test_mode=True)

try:
    driver.start()
    print('TUI started.', file=sys.stderr)
    
    deadline = time.monotonic() + 10
    ready = False
    while time.monotonic() < deadline:
        raw = driver.capture_raw(timeout=0.5)
        if raw and b'Idle' in raw:
            ready = True
            break
    print(f'Ready: {ready}', file=sys.stderr)
    
    if ready:
        driver.send_keys('run a tool')
        driver.send_enter()
        print('Goal submitted. Waiting 8s...', file=sys.stderr)
        time.sleep(8)
    
    txt = driver.capture(timeout=3)
    print(f'FINAL OUTPUT: {txt[:500]}', file=sys.stderr)
    
finally:
    driver.stop()
    ms.terminate()
    ms.wait()
    log_path = f'{a0_dir}/a0.log'
    if os.path.exists(log_path):
        print(file=sys.stderr)
        print('=== LOG FILE ===', file=sys.stderr)
        for line in open(log_path):
            if 'TRACE' in line or 'CORE' in line or 'TUI' in line:
                print(line.rstrip(), file=sys.stderr)
    else:
        print('NO LOG FILE', file=sys.stderr)
