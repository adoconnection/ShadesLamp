"""Delete one or more programs from the lamp over BLE (CMD_DELETE_PROGRAM).

Usage: python delete_program.py 55 53
"""
import asyncio
import sys
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"
CMD_DELETE_PROGRAM = 0x12
CMD_GET_PROGRAMS = 0x01


class Asm:
    def __init__(self):
        self.buf = bytearray(); self.fut = None
    def handle(self, _c, data):
        if len(data) < 2: return
        self.buf.extend(data[2:])
        if data[1] & 0x01 and self.fut and not self.fut.done():
            self.fut.set_result(bytes(self.buf))
    async def req(self, c, payload, timeout=8.0):
        self.buf = bytearray(); self.fut = asyncio.get_running_loop().create_future()
        await c.write_gatt_char(CHAR_COMMAND, payload, response=True)
        return (await asyncio.wait_for(self.fut, timeout)).decode("utf-8", "replace")


async def main():
    ids = [int(x) for x in sys.argv[1:]]
    if not ids:
        print("usage: delete_program.py <id> [id...]", file=sys.stderr); sys.exit(1)

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)
    async with BleakClient(dev) as c:
        a = Asm()
        await c.start_notify(CHAR_RESPONSE, a.handle)
        for pid in ids:
            r = await a.req(c, bytes([CMD_DELETE_PROGRAM, pid]))
            print(f"DELETE {pid} -> {r}", flush=True)
            await asyncio.sleep(0.5)  # let the deferred wipe run on the render task
        # Verify
        progs = await a.req(c, bytes([CMD_GET_PROGRAMS]), timeout=20.0)
        import json
        try:
            arr = json.loads(progs)
            remaining = sorted(p["id"] for p in arr)
            print(f"remaining {len(arr)} programs: {remaining}")
            for pid in ids:
                print(f"  {pid} deleted:", pid not in remaining)
        except Exception:
            print("GET_PROGRAMS ->", progs[:200])


if __name__ == "__main__":
    asyncio.run(main())
