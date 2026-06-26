"""Smoke-test the generic file BLE commands (write/list/read/delete)."""
import asyncio, sys
from bleak import BleakScanner, BleakClient

SVC="0000ff00-0000-1000-8000-00805f9b34fb"
CMD="0000ff01-0000-1000-8000-00805f9b34fb"
RSP="0000ff02-0000-1000-8000-00805f9b34fb"
WRITE,DEL,GET,LIST = 0x2F,0x30,0x2D,0x2E

class A:
    def __init__(s): s.buf=bytearray(); s.fut=None
    def h(s,_c,d):
        if len(d)<2: return
        s.buf.extend(d[2:])
        if d[1]&1 and s.fut and not s.fut.done(): s.fut.set_result(bytes(s.buf))
    async def req(s,c,payload,to=8):
        s.buf=bytearray(); s.fut=asyncio.get_running_loop().create_future()
        await c.write_gatt_char(CMD,payload,response=True)
        return (await asyncio.wait_for(s.fut,to)).decode("utf-8","replace")

async def main():
    dev=await BleakScanner.find_device_by_filter(lambda d,ad: SVC in (ad.service_uuids or []),timeout=15)
    if not dev: print("no lamp"); sys.exit(2)
    async with BleakClient(dev) as c:
        a=A(); await c.start_notify(RSP,a.h)
        path=b"/playlists/test/0"
        data=b'{"prog":5,"params":{"0":3,"1":120}}'
        wpayload=bytes([WRITE,len(path)])+path+data
        print("WRITE  ->", await a.req(c,wpayload))
        print("LIST   ->", await a.req(c,bytes([LIST])+b"/playlists"))
        print("LISTd  ->", await a.req(c,bytes([LIST])+b"/playlists/test"))
        print("GET    ->", await a.req(c,bytes([GET])+path))
        print("DELETE ->", await a.req(c,bytes([DEL])+b"/playlists/test"))
        print("LIST2  ->", await a.req(c,bytes([LIST])+b"/playlists"))

asyncio.run(main())
