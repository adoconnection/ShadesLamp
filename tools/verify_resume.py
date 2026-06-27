"""Verify power-on resume (build >=12) over BLE, end to end.

A soft reboot (CMD_REBOOT) re-runs setup() exactly like a power cycle, so it
exercises the same begin()/resumeFromState() path — no physical power needed.

Test 1 (program): Set Active a program -> wait out the 10 s debounce -> reboot
                  -> reconnect -> assert the same program is active.
Test 2 (playlist): PL_PLAY a playlist -> wait out the debounce -> reboot
                  -> reconnect -> assert the same playlist is playing.

Run this the moment the lamp is back in BLE range (after flashing build 12):
    python tools/ota_ble.py && python tools/verify_resume.py
"""
import asyncio
import json
import sys

from bleak import BleakScanner, BleakClient

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"
CMD = "0000ff01-0000-1000-8000-00805f9b34fb"
RSP = "0000ff02-0000-1000-8000-00805f9b34fb"
ACT = "0000ff03-0000-1000-8000-00805f9b34fb"

CMD_GET_PROGRAMS = 0x01
CMD_REBOOT = 0x24
CMD_PL_LIST = 0x32
CMD_PL_PLAY = 0x3C
CMD_PL_STATE = 0x3E

DEBOUNCE_WAIT = 12.0   # > RESUME_DEBOUNCE_MS (10 s) so the save flushes
REBOOT_WAIT = 9.0      # time for the lamp to restart and re-advertise


class Asm:
    def __init__(self): self.buf = bytearray(); self.future = None; self.error = False
    def handle(self, _c, d):
        if len(d) < 2: return
        self.buf.extend(d[2:])
        if d[1] & 0x02: self.error = True
        if d[1] & 0x01 and self.future and not self.future.done():
            self.future.set_result(bytes(self.buf))
    async def req(self, c, p, t=15.0):
        loop = asyncio.get_running_loop(); self.buf = bytearray(); self.error = False
        self.future = loop.create_future()
        await c.write_gatt_char(CMD, p, response=True)
        raw = await asyncio.wait_for(self.future, t)
        return raw.decode("utf-8", "replace"), self.error


async def find():
    return await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in (ad.service_uuids or []), timeout=20.0)


async def connect():
    for attempt in range(6):
        dev = await find()
        if dev:
            try:
                c = BleakClient(dev)
                await c.connect()
                return c
            except Exception as e:
                print(f"  connect retry ({e})", flush=True)
        await asyncio.sleep(2.0)
    raise RuntimeError("lamp not reachable")


async def reboot_and_reconnect(c, a):
    print(f"  rebooting... (wait {REBOOT_WAIT}s)", flush=True)
    try:
        await c.write_gatt_char(CMD, bytes([CMD_REBOOT]), response=False)
    except Exception:
        pass
    try:
        await c.disconnect()
    except Exception:
        pass
    await asyncio.sleep(REBOOT_WAIT)
    c2 = await connect()
    await c2.start_notify(RSP, a.handle)
    return c2


async def main():
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass

    a = Asm()
    print("Connecting...", flush=True)
    c = await connect()
    await c.start_notify(RSP, a.handle)

    passed = True

    # ── Test 1: program resume ──────────────────────────────────────────────
    progs = json.loads((await a.req(c, bytes([CMD_GET_PROGRAMS])))[0])
    cur = (await c.read_gatt_char(ACT))[0]
    target = next((p["id"] for p in progs if p["id"] != cur), progs[0]["id"])
    tname = next((p.get("name") for p in progs if p["id"] == target), "?")
    print(f"\n[1] Program resume: Set Active id={target} ({tname!r}) "
          f"(was {cur})", flush=True)
    await c.write_gatt_char(ACT, bytes([target]), response=True)
    print(f"  waiting {DEBOUNCE_WAIT}s for the debounce to flush...", flush=True)
    await asyncio.sleep(DEBOUNCE_WAIT)
    c = await reboot_and_reconnect(c, a)
    after = (await c.read_gatt_char(ACT))[0]
    ok1 = (after == target)
    print(f"  after reboot active = {after}  -> {'PASS' if ok1 else 'FAIL'}", flush=True)
    passed &= ok1

    # ── Test 2: playlist resume ─────────────────────────────────────────────
    pls = json.loads((await a.req(c, bytes([CMD_PL_LIST])))[0])
    pls = [p for p in pls if p.get("count", 0) > 0]
    if not pls:
        print("\n[2] Playlist resume: SKIP (no non-empty playlists)", flush=True)
    else:
        plid = pls[0]["id"]; plname = pls[0].get("name")
        print(f"\n[2] Playlist resume: PL_PLAY id={plid} ({plname!r})", flush=True)
        await a.req(c, bytes([CMD_PL_PLAY, plid & 0xFF, 0]))
        print(f"  waiting {DEBOUNCE_WAIT}s for the debounce to flush...", flush=True)
        await asyncio.sleep(DEBOUNCE_WAIT)
        c = await reboot_and_reconnect(c, a)
        st = json.loads((await a.req(c, bytes([CMD_PL_STATE])))[0])
        ok2 = (st.get("playing") == plid)
        print(f"  after reboot PL_STATE = {st}  -> {'PASS' if ok2 else 'FAIL'}", flush=True)
        passed &= ok2

    print(f"\n=== {'ALL PASS' if passed else 'FAILURES PRESENT'} ===", flush=True)
    await c.disconnect()
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    asyncio.run(main())
