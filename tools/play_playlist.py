"""Start a playlist on the lamp over BLE (lamp-driven rotation).

Usage: python play_playlist.py [playlistId] [startIndex]
If no playlistId is given, the first playlist that has positions is used.
"""
import asyncio
import json
import sys

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"

CMD_PL_LIST = 0x32
CMD_PL_PLAY = 0x3C
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
    want_id = int(sys.argv[1]) if len(sys.argv) > 1 else None
    start_index = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    print("Scanning...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)

    print(f"Connecting {dev.address}...", flush=True)
    async with BleakClient(dev) as c:
        a = Assembler()
        await c.start_notify(CHAR_RESPONSE, a.handle)

        text, err = await a.request(c, bytes([CMD_PL_LIST]))
        print("PL_LIST ->", text, flush=True)
        lists = json.loads(text) if not err else []

        if want_id is None:
            with_pos = [p for p in lists if p.get("count", 0) > 0]
            chosen = (with_pos or lists)
            if not chosen:
                print("no playlists on the lamp", file=sys.stderr); sys.exit(3)
            want_id = chosen[0]["id"]

        meta = next((p for p in lists if p.get("id") == want_id), None)
        print(f"Playing playlist id={want_id} index={start_index} "
              f"({meta}) ...", flush=True)

        text, err = await a.request(c, bytes([CMD_PL_PLAY, want_id & 0xFF, start_index & 0xFF]))
        print("PL_PLAY ->", text, flush=True)

        await asyncio.sleep(0.3)
        text, err = await a.request(c, bytes([CMD_PL_STATE]))
        print("PL_STATE ->", text, flush=True)


if __name__ == "__main__":
    asyncio.run(main())
