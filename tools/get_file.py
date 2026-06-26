"""Read a file off the lamp over BLE (GET_FILE) and print it as UTF-8.

Usage: python get_file.py /programs/55/meta.json [out.json]
"""
import asyncio
import json
import sys
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"
CMD_GET_FILE = 0x2D


async def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/config.json"
    out = sys.argv[2] if len(sys.argv) > 2 else None

    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)
    async with BleakClient(dev) as c:
        buf = bytearray(); fut = None
        def on(_x, data):
            nonlocal fut
            if len(data) < 2: return
            buf.extend(data[2:])
            if data[1] & 0x01 and fut and not fut.done():
                fut.set_result(bytes(buf))
        await c.start_notify(CHAR_RESPONSE, on)
        loop = asyncio.get_running_loop(); fut = loop.create_future()
        await c.write_gatt_char(CHAR_COMMAND, bytes([CMD_GET_FILE]) + path.encode(), response=True)
        raw = await asyncio.wait_for(fut, 15)

    text = raw.decode("utf-8", "replace")
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"saved {len(raw)} bytes to {out}")
    # Safe summary regardless of console encoding
    try:
        meta = json.loads(text)
        print("keys:", list(meta.keys()))
        i18n = meta.get("i18n")
        if isinstance(i18n, dict):
            print("i18n locales:", list(i18n.keys()))
            ru = i18n.get("ru")
            print("has ru:", ru is not None)
            if isinstance(ru, dict):
                print("ru keys:", list(ru.keys()))
        else:
            print("no i18n object")
    except Exception as e:
        print("not JSON / parse error:", e)
        print(text.encode("ascii", "backslashreplace").decode("ascii")[:500])


if __name__ == "__main__":
    asyncio.run(main())
