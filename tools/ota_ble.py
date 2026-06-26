"""Flash a firmware .bin to the lamp over BLE (OTA via the upload pipeline, type=2).

Usage: python ota_ble.py [path-to-firmware.ino.bin]
"""
import asyncio
import os
import struct
import sys
import time

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"
CHAR_UPLOAD = "0000ff04-0000-1000-8000-00805f9b34fb"

CMD_UPLOAD_START = 0x10
CMD_UPLOAD_FINISH = 0x11
UPLOAD_TYPE_FIRMWARE = 2

# Tunable via env: OTA_CHUNK (bytes/write), OTA_DELAY (s between writes),
# OTA_BURST (writes between pauses). Defaults pick a fast-but-safe profile that
# relies on the build>=3 throttled progress bar (no per-chunk show()).
CHUNK = int(os.environ.get("OTA_CHUNK", "480"))
PER_CHUNK_DELAY = float(os.environ.get("OTA_DELAY", "0.004"))
BURST = int(os.environ.get("OTA_BURST", "1"))

DEFAULT_BIN = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "firmware", "build", "esp32.esp32.esp32s3", "firmware.ino.bin",
)


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
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BIN
    with open(path, "rb") as f:
        img = f.read()
    print(f"Firmware: {path}\nSize: {len(img)} bytes", flush=True)

    print("Scanning...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)
    print(f"chunk={CHUNK}B delay={PER_CHUNK_DELAY*1000:.0f}ms burst={BURST}", flush=True)
    total = (len(img) + CHUNK - 1) // CHUNK

    for attempt in range(1, 4):
        print(f"Connecting {dev.address}... (attempt {attempt})", flush=True)
        try:
            async with BleakClient(dev) as c:
                a = Assembler()
                await c.start_notify(CHAR_RESPONSE, a.handle)

                start = bytes([CMD_UPLOAD_START]) + struct.pack("<I", len(img)) + bytes([UPLOAD_TYPE_FIRMWARE])
                text, err = await a.request(c, start)
                print("UPLOAD_START ->", text, flush=True)
                if err or '"ok":true' not in text:
                    print("start rejected", file=sys.stderr); sys.exit(3)

                t0 = time.monotonic()
                for i in range(total):
                    chunk = img[i * CHUNK:(i + 1) * CHUNK]
                    await c.write_gatt_char(CHAR_UPLOAD, chunk, response=False)
                    if BURST <= 1 or i % BURST == BURST - 1:
                        await asyncio.sleep(PER_CHUNK_DELAY)
                    if i % 400 == 0 or i == total - 1:
                        print(f"  {(i+1)*100//total:3d}%  ({i+1}/{total})", flush=True)
                print(f"Uploaded {len(img)} bytes in {time.monotonic()-t0:.1f}s", flush=True)

                try:
                    text, err = await a.request(c, bytes([CMD_UPLOAD_FINISH]), timeout=15.0)
                    print("UPLOAD_FINISH ->", text, flush=True)
                except asyncio.TimeoutError:
                    print("UPLOAD_FINISH -> (no response; device likely flashing/rebooting)", flush=True)
                return
        except Exception as e:
            print(f"  link error: {e}; retrying...", flush=True)
            await asyncio.sleep(2.0)
    print("OTA failed after retries", file=sys.stderr); sys.exit(4)


if __name__ == "__main__":
    asyncio.run(main())
