import asyncio
from bleak import BleakScanner

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"

async def main():
    print("Active scan for 12s...", flush=True)
    devices = await BleakScanner.discover(timeout=12.0, return_adv=True, scanning_mode="active")
    found = False
    print(f"\nSeen {len(devices)} device(s):")
    for addr, (dev, adv) in sorted(devices.items(), key=lambda kv: -(kv[1][1].rssi or -999)):
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        is_lamp = SVC in uuids
        found = found or is_lamp
        name = adv.local_name or dev.name or "(no name)"
        mfg = {k: v.hex() for k, v in (adv.manufacturer_data or {}).items()}
        sd  = {k: v.hex() for k, v in (adv.service_data or {}).items()}
        tag = "  <-- SHADES LAMP" if is_lamp else ""
        print(f"  {addr}  rssi={adv.rssi}  name={name}{tag}")
        if uuids: print(f"      uuids: {uuids}")
        if mfg:   print(f"      mfg:   {mfg}")
        if sd:    print(f"      sdata: {sd}")
    print()
    print("LAMP FOUND" if found else "Lamp NOT found (service 0xff00 not advertised)")

asyncio.run(main())
