"""Set a playlist's rotation mode/interval on the lamp over BLE.

Usage: python set_rotation.py <playlistId> <mode> <intervalSec>
  mode: 0=off, 1=next (in order), 2=random
"""
import asyncio
import struct
import sys

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"

CMD_PL_LIST = 0x32
CMD_PL_SET_ROTATION = 0x37
CMD_PL_STATE = 0x3E


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

    async def request(self, client, payload, timeout=10.0):
        loop = asyncio.get_running_loop()
        self.buf = bytearray(); self.error = False
        self.future = loop.create_future()
        await client.write_gatt_char(CHAR_COMMAND, payload, response=True)
        raw = await asyncio.wait_for(self.future, timeout)
        return raw.decode("utf-8", "replace"), self.error


async def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    pid = int(sys.argv[1]); mode = int(sys.argv[2]); interval = int(sys.argv[3])

    print("Scanning...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)

    print(f"Connecting {dev.address}...", flush=True)
    async with BleakClient(dev) as c:
        a = Assembler()
        await c.start_notify(CHAR_RESPONSE, a.handle)

        payload = bytes([CMD_PL_SET_ROTATION, pid & 0xFF, mode & 0xFF]) + struct.pack("<H", interval)
        text, err = await a.request(c, payload)
        print(f"PL_SET_ROTATION id={pid} mode={mode} interval={interval}s ->", text, flush=True)

        text, err = await a.request(c, bytes([CMD_PL_LIST]))
        print("PL_LIST ->", text, flush=True)
        text, err = await a.request(c, bytes([CMD_PL_STATE]))
        print("PL_STATE ->", text, flush=True)


if __name__ == "__main__":
    asyncio.run(main())
