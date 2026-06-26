"""Push the hardware config from firmware/config.default.json to the lamp over BLE.

Used to restore the LED matrix layout after a USB reflash wipes LittleFS.
Sends SET_HW_CONFIG then REBOOT so the new config is applied.
"""
import asyncio
import json
import os
import struct
import sys

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"

CMD_SET_HW_CONFIG = 0x23
CMD_REBOOT = 0x24

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CONFIG = os.path.join(HERE, "..", "firmware", "config.default.json")


async def request(client, asm_future, payload, timeout=5.0):
    fut = asm_future()
    await client.write_gatt_char(CHAR_COMMAND, payload, response=True)
    return await asyncio.wait_for(fut, timeout)


async def main():
    with open(DEFAULT_CONFIG, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    pin = cfg["ledPin"]
    w = cfg["ledWidth"]
    h = cfg["ledHeight"]
    zig = 1 if cfg["ledZigzag"] else 0
    order = cfg["ledColorOrder"]
    print(f"Restoring HW: pin={pin} {w}x{h} zigzag={zig} order={order}", flush=True)

    print("Scanning for Shades Lamp...", flush=True)
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0
    )
    if device is None:
        print("ERROR: lamp not found", file=sys.stderr)
        sys.exit(2)

    print(f"Connecting to {device.name} [{device.address}]...", flush=True)
    async with BleakClient(device) as client:
        buf = bytearray()
        fut = None

        def on_notify(_c, data):
            nonlocal fut
            if len(data) < 2:
                return
            buf.extend(data[2:])
            if data[1] & 0x01 and fut and not fut.done():  # FINAL
                fut.set_result(bytes(buf))

        def new_future():
            nonlocal fut
            buf.clear()
            loop = asyncio.get_running_loop()
            fut = loop.create_future()
            return fut

        await client.start_notify(CHAR_RESPONSE, on_notify)

        payload = bytes([CMD_SET_HW_CONFIG, pin]) + struct.pack("<HH", w, h) + bytes([zig, order])
        resp = await request(client, new_future, payload)
        print("SET_HW_CONFIG ->", resp.decode("utf-8", "replace"), flush=True)

        # Reboot to apply
        try:
            await request(client, new_future, bytes([CMD_REBOOT]), timeout=2.0)
        except asyncio.TimeoutError:
            pass  # device reboots, may not answer
        print("Reboot sent. HW config restored.", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
