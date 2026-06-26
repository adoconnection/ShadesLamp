"""Diagnose the lamp's program list over BLE using the proven chunk assembler.

Warms up with GET_HW_CONFIG (known-good), then probes GET_PROGRAMS, GET_STORAGE,
and reads a couple of files via GET_FILE to confirm what's actually on flash.
"""
import asyncio
import sys
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"

CMD_GET_PROGRAMS = 0x01
CMD_GET_HW_CONFIG = 0x22
CMD_GET_STORAGE = 0x29
CMD_GET_FILE = 0x2D

FLAG_FINAL = 0x01
FLAG_ERROR = 0x02


class Assembler:
    def __init__(self):
        self.buf = bytearray(); self.error = False; self.future = None

    def handle(self, _c, data):
        if len(data) < 2:
            return
        self.count = getattr(self, "count", 0) + 1
        self.last_seq = data[0]; self.last_flags = data[1]
        self.buf.extend(data[2:])
        if data[1] & FLAG_ERROR:
            self.error = True
        if data[1] & FLAG_FINAL and self.future and not self.future.done():
            self.future.set_result(bytes(self.buf))

    async def request(self, client, payload, timeout=6.0):
        loop = asyncio.get_running_loop()
        self.buf = bytearray(); self.error = False; self.count = 0
        self.future = loop.create_future()
        await client.write_gatt_char(CHAR_COMMAND, payload, response=True)
        try:
            raw = await asyncio.wait_for(self.future, timeout)
            return raw.decode("utf-8", "replace"), self.error
        except asyncio.TimeoutError:
            print(f"   (timeout after {self.count} chunks, {len(self.buf)}B, "
                  f"last seq={getattr(self,'last_seq','-')} flags={getattr(self,'last_flags',0):#04x})", flush=True)
            raise


async def main():
    print("Scanning...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)
    print(f"Connecting {dev.address}...", flush=True)
    async with BleakClient(dev) as c:
        a = Assembler()
        await c.start_notify(CHAR_RESPONSE, a.handle)

        def safe(s):
            return s.encode("ascii", "backslashreplace").decode("ascii")

        async def probe(label, payload, timeout=6.0):
            try:
                text, err = await a.request(c, payload, timeout)
                print(f"{label} -> {'ERR ' if err else ''}[{len(text)}B] {safe(text)[:300]}", flush=True)
                return text
            except asyncio.TimeoutError:
                print(f"{label} -> TIMEOUT", flush=True)
                return None

        await probe("GET_HW_CONFIG", bytes([CMD_GET_HW_CONFIG]))
        await probe("GET_PROGRAMS ", bytes([CMD_GET_PROGRAMS]), 20.0)
        await probe("LIST /programs", bytes([0x2E]) + b"/programs")


if __name__ == "__main__":
    asyncio.run(main())
