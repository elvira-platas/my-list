"""Check real process contention and restart after a crash, with isolated data."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


binary = Path(sys.argv[1] if len(sys.argv) > 1 else "./writer-todo").resolve()
with tempfile.TemporaryDirectory(prefix="my-list-instance-") as directory:
    root = Path(directory)
    env = dict(os.environ, XDG_DATA_HOME=str(root / "data"),
               XDG_CONFIG_HOME=str(root / "config"), QT_QPA_PLATFORM="offscreen")
    folder = root / "data" / "Elvira" / "writer-todo"
    folder.mkdir(parents=True)
    tasks = folder / "tasks.json"
    original = b'[{"html":"Keep the dragon safe","done":false}]'
    tasks.write_bytes(original)
    lock = folder / "instance.lock"

    def start():
        process = subprocess.Popen([str(binary)], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                assert process.poll() is None, "application exited during startup"
                if lock.exists() and lock.read_text().splitlines()[:1] == [str(process.pid)]:
                    return process
                time.sleep(0.02)
            raise AssertionError("application did not acquire its lock")
        except BaseException:
            process.kill()
            process.wait()
            raise

    first = start()
    try:
        # An old modification time must not invalidate a live process's lock.
        os.utime(lock, (1, 1))
        second = subprocess.run([str(binary)], env=env, capture_output=True, timeout=5)
        assert second.returncode == 0, second.stderr
        assert b"already running" in second.stderr, second.stderr
        assert first.poll() is None, "second launch stopped the original process"
        assert tasks.read_bytes() == original, "second launch changed tasks"
    finally:
        first.kill()
        first.wait()

    assert lock.exists(), "crash scenario must leave a stale lock"
    restarted = start()
    try:
        assert tasks.read_bytes() == original, "restart changed saved tasks"
    finally:
        restarted.kill()
        restarted.wait()

print("Single-instance checks passed")
