"""Pull the live device config off a Shades Lamp over BLE and write config.json.

Reconstructs /config.json from the BLE API (GET_HW_CONFIG + GET_NAME + active
program), since the firmware exposes those fields individually rather than the
raw file. Response chunking matches ble_service.cpp: [seq][flags][payload],
flags bit0=FINAL, bit1=ERROR.
"""
import asyncio
import json
import sys

from bleak import BleakScanner, BleakClient

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE = "0000ff02-0000-1000-8000-00805f9b34fb"
CHAR_ACTIVE = "0000ff03-0000-1000-8000-00805f9b34fb"

CMD_GET_NAME = 0x21
CMD_GET_HW_CONFIG = 0x22

FLAG_FINAL = 0x01
FLAG_ERROR = 0x02


class ResponseAssembler:
    def __init__(self):
        self.buf = bytearray()
        self.error = False
        self.future: asyncio.Future | None = None

    def handle(self, _char, data: bytearray):
        if len(data) < 2:
            return
        flags = data[1]
        self.buf.extend(data[2:])
        if flags & FLAG_ERROR:
            self.error = True
        if flags & FLAG_FINAL and self.future and not self.future.done():
            self.future.set_result(bytes(self.buf))

    async def request(self, client: BleakClient, payload: bytes, timeout=5.0) -> str:
        loop = asyncio.get_running_loop()
        self.buf = bytearray()
        self.error = False
        self.future = loop.create_future()
        await client.write_gatt_char(CHAR_COMMAND, payload, response=True)
        raw = await asyncio.wait_for(self.future, timeout)
        text = raw.decode("utf-8", errors="replace")
        if self.error:
            raise RuntimeError(f"device returned error: {text}")
        return text


async def main():
    print("Scanning for Shades Lamp (service 0xff00)...", flush=True)
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE_UUID in (ad.service_uuids or []),
        timeout=15.0,
    )
    if device is None:
        print("ERROR: no lamp found advertising the service UUID.", file=sys.stderr)
        sys.exit(2)

    print(f"Found {device.name} [{device.address}], connecting...", flush=True)
    async with BleakClient(device) as client:
        asm = ResponseAssembler()
        await client.start_notify(CHAR_RESPONSE, asm.handle)

        hw_text = await asm.request(client, bytes([CMD_GET_HW_CONFIG]))
        name_text = await asm.request(client, bytes([CMD_GET_NAME]))
        active_raw = await client.read_gatt_char(CHAR_ACTIVE)
        active = active_raw[0] if active_raw else 255

        await client.stop_notify(CHAR_RESPONSE)

    hw = json.loads(hw_text)
    name = json.loads(name_text).get("name", "Shades LED Lamp")

    # Mirror exactly what ProgramManager::saveConfig() writes.
    config = {
        "active": active,
        "name": name,
        "ledPin": hw["pin"],
        "ledWidth": hw["width"],
        "ledHeight": hw["height"],
        "ledZigzag": hw["zigzag"],
        "ledColorOrder": hw["colorOrder"],
    }

    print("\nLive HW config:", json.dumps(hw, ensure_ascii=False))
    print("Reconstructed config.json:", json.dumps(config, ensure_ascii=False))

    # Emit to stdout as the last line so the caller can capture it.
    print("CONFIG_JSON=" + json.dumps(config, ensure_ascii=False))


if __name__ == "__main__":
    asyncio.run(main())
