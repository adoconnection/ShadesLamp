"""Listen to CHAR_EVENTS and time the gaps between playlist auto-advances.

Measures the REAL rotation interval on the device: each EVT_PL_ADVANCE (0x03)
is timestamped and the delta from the previous advance is printed.

Usage: python events_monitor.py [seconds]   (default 150)
"""
import asyncio
import sys
import time

from bleak import BleakScanner, BleakClient

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"
CMD = "0000ff01-0000-1000-8000-00805f9b34fb"
RSP = "0000ff02-0000-1000-8000-00805f9b34fb"
EVT = "0000ff06-0000-1000-8000-00805f9b34fb"

CMD_PL_STATE = 0x3E
EVT_PL_ADVANCE = 0x03
EVT_PL_STOPPED = 0x04
NAMES = {0x01: "PROG_ADDED", 0x02: "PROG_DELETED", 0x03: "PL_ADVANCE", 0x04: "PL_STOPPED"}


async def main():
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in (ad.service_uuids or []), timeout=18.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)

    last = {"t": None}
    advances = []

    def on_evt(_c, data):
        if len(data) < 2:
            return
        t = time.monotonic()
        typ, val = data[0], data[1]
        name = NAMES.get(typ, hex(typ))
        if typ == EVT_PL_ADVANCE:
            delta = None if last["t"] is None else t - last["t"]
            last["t"] = t
            advances.append(t)
            ds = f"  (+{delta:.1f}s since previous advance)" if delta is not None else "  (first)"
            print(f"[{t:7.1f}] {name} index={val}{ds}", flush=True)
        else:
            print(f"[{t:7.1f}] {name} arg={val}", flush=True)

    async with BleakClient(dev) as c:
        await c.start_notify(EVT, on_evt)
        # Show what's playing + the configured interval up front.
        rbuf = bytearray(); fut = asyncio.get_running_loop().create_future()
        def on_rsp(_c, d):
            if len(d) < 2: return
            rbuf.extend(d[2:])
            if d[1] & 0x01 and not fut.done(): fut.set_result(bytes(rbuf))
        await c.start_notify(RSP, on_rsp)
        await c.write_gatt_char(CMD, bytes([CMD_PL_STATE]), response=True)
        try:
            st = (await asyncio.wait_for(fut, 8)).decode("utf-8", "replace")
            print(f"PL_STATE: {st}", flush=True)
        except Exception:
            pass

        print(f"Listening {duration:.0f}s for auto-advances...", flush=True)
        await asyncio.sleep(duration)

    if len(advances) >= 2:
        gaps = [advances[i] - advances[i - 1] for i in range(1, len(advances))]
        avg = sum(gaps) / len(gaps)
        print(f"\n{len(advances)} advances; gaps = "
              f"{', '.join(f'{g:.1f}' for g in gaps)}s; avg = {avg:.1f}s", flush=True)
    else:
        print(f"\nOnly {len(advances)} advance(s) seen in {duration:.0f}s.", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
