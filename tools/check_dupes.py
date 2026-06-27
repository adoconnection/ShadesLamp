"""Find (and optionally delete) duplicate installed programs on the lamp over BLE.

A duplicate = same program installed under more than one id. Grouped by `guid`
(stable per program); programs with no guid fall back to grouping by name.

Usage:
  python check_dupes.py            # report only
  python check_dupes.py --delete   # delete extras, keeping the FIRST id per group
"""
import asyncio
import json
import sys

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"

CMD_GET_PROGRAMS = 0x01
CMD_DELETE_PROGRAM = 0x12


class Assembler:
    def __init__(self):
        self.buf = bytearray(); self.future = None; self.error = False

    def handle(self, _c, data):
        if len(data) < 2:
            return
        self.buf.extend(data[2:])
        if data[1] & 0x02:
            self.error = True
        if data[1] & 0x01 and self.future and not self.future.done():
            self.future.set_result(bytes(self.buf))

    async def request(self, client, payload, timeout=20.0):
        loop = asyncio.get_running_loop()
        self.buf = bytearray(); self.error = False
        self.future = loop.create_future()
        await client.write_gatt_char(CHAR_COMMAND, payload, response=True)
        raw = await asyncio.wait_for(self.future, timeout)
        return raw.decode("utf-8", "replace"), self.error


def group_key(p):
    g = p.get("guid")
    if g:
        return ("guid", g)
    return ("name", p.get("name", ""))


async def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    do_delete = "--delete" in sys.argv[1:]

    print("Scanning...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)

    print(f"Connecting {dev.address}...", flush=True)
    async with BleakClient(dev) as c:
        a = Assembler()
        await c.start_notify(CHAR_RESPONSE, a.handle)

        text, err = await a.request(c, bytes([CMD_GET_PROGRAMS]))
        if err:
            print("GET_PROGRAMS error:", text, file=sys.stderr); sys.exit(3)
        progs = json.loads(text)
        print(f"{len(progs)} programs installed:")
        for p in progs:
            print(f"  id={p['id']:>3}  {p.get('name','?')!r}  guid={p.get('guid','-')} v={p.get('version','-')}")

        groups = {}
        for p in progs:
            groups.setdefault(group_key(p), []).append(p)

        dupes = {k: v for k, v in groups.items() if len(v) > 1}
        if not dupes:
            print("\nNo duplicates found.")
            return

        print(f"\n{len(dupes)} duplicated program(s):")
        to_delete = []
        for k, members in dupes.items():
            members_sorted = members  # keep server (display) order; first one is kept
            keep = members_sorted[0]
            drop = members_sorted[1:]
            label = k[1]
            print(f"  [{k[0]}={label}]  keep id={keep['id']} ({keep.get('name')!r}); "
                  f"delete ids={[d['id'] for d in drop]}")
            to_delete.extend(drop)

        if not do_delete:
            print(f"\nReport only. Re-run with --delete to remove {len(to_delete)} extra copy(ies).")
            return

        print(f"\nDeleting {len(to_delete)} extra copy(ies)...")
        for d in to_delete:
            text, err = await a.request(c, bytes([CMD_DELETE_PROGRAM, d['id'] & 0xFF]))
            print(f"  DELETE id={d['id']} ({d.get('name')!r}) -> {'ERR ' if err else ''}{text}")

        text, err = await a.request(c, bytes([CMD_GET_PROGRAMS]))
        progs2 = json.loads(text)
        print(f"\nNow {len(progs2)} programs installed.")


if __name__ == "__main__":
    asyncio.run(main())
