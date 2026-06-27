"""Wait until the lamp is reachable over BLE, then flash build 12 and verify resume.

Polls for the service UUID every POLL_SEC. Once the lamp is seen, runs
ota_ble.py (flash) and then verify_resume.py (resume PASS/FAIL). Exits 0 only
if both the flash and the verification succeed.

Run in the background; it self-completes the moment the lamp comes back in range.
"""
import asyncio
import os
import subprocess
import sys
import time

from bleak import BleakScanner

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"
HERE = os.path.dirname(os.path.abspath(__file__))
POLL_SEC = 30
MAX_MINUTES = 90


async def reachable():
    d = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in (ad.service_uuids or []), timeout=12.0)
    return d is not None


def run(script):
    print(f"\n=== running {script} ===", flush=True)
    r = subprocess.run([sys.executable, os.path.join(HERE, script)])
    print(f"=== {script} exit={r.returncode} ===", flush=True)
    return r.returncode


async def main():
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass

    deadline = time.monotonic() + MAX_MINUTES * 60
    print(f"Waiting for the lamp (poll {POLL_SEC}s, up to {MAX_MINUTES} min)...", flush=True)
    seen = False
    while time.monotonic() < deadline:
        try:
            if await reachable():
                seen = True
                break
        except Exception as e:
            print(f"  scan error: {e}", flush=True)
        await asyncio.sleep(POLL_SEC)

    if not seen:
        print("Lamp did not appear within the window; still unreachable.", flush=True)
        sys.exit(2)

    print("Lamp is in range — deploying.", flush=True)
    # A couple of OTA attempts in case the link is briefly weak on first contact.
    flashed = False
    for attempt in range(1, 4):
        if run("ota_ble.py") == 0:
            flashed = True
            break
        print(f"  OTA attempt {attempt} failed; rescanning...", flush=True)
        for _ in range(10):
            if await reachable():
                break
            await asyncio.sleep(POLL_SEC)
    if not flashed:
        print("OTA did not complete; lamp reachable but flashing failed.", flush=True)
        sys.exit(3)

    # Give the device a moment to reboot into the new build before verifying.
    await asyncio.sleep(8)
    rc = run("verify_resume.py")
    sys.exit(0 if rc == 0 else 4)


if __name__ == "__main__":
    asyncio.run(main())
