"""Validate the playlist storage round-trip over BLE, mirroring the app's writes."""
import asyncio, sys
from bleak import BleakScanner, BleakClient
SVC="0000ff00-0000-1000-8000-00805f9b34fb"
CMD="0000ff01-0000-1000-8000-00805f9b34fb"; RSP="0000ff02-0000-1000-8000-00805f9b34fb"
WRITE,DEL,GET,LIST=0x2F,0x30,0x2D,0x2E

class A:
    def __init__(s): s.buf=bytearray(); s.fut=None
    def h(s,_c,d):
        if len(d)<2: return
        s.buf.extend(d[2:])
        if d[1]&1 and s.fut and not s.fut.done(): s.fut.set_result(bytes(s.buf))
    async def req(s,c,p,to=8):
        s.buf=bytearray(); s.fut=asyncio.get_running_loop().create_future()
        await c.write_gatt_char(CMD,p,response=True)
        return (await asyncio.wait_for(s.fut,to)).decode("utf-8","replace")

def wr(path, data):
    pb=path.encode(); db=data.encode()
    return bytes([WRITE,len(pb)])+pb+db

async def main():
    dev=await BleakScanner.find_device_by_filter(lambda d,ad: SVC in (ad.service_uuids or []),timeout=15)
    if not dev: print("no lamp"); sys.exit(2)
    async with BleakClient(dev) as c:
        a=A(); await c.start_notify(RSP,a.h)
        print("play  ->", await a.req(c, wr("/playlists/9/play", '{"name":"Test","mode":"next","interval":10,"order":["0","1"]}')))
        print("pos0  ->", await a.req(c, wr("/playlists/9/0", '{"prog":5,"slug":"flame","name":"Flame","params":[{"id":0,"value":3,"f":false}]}')))
        print("pos1  ->", await a.req(c, wr("/playlists/9/1", '{"prog":7,"slug":"warp","name":"Warp","params":[]}')))
        print("LIST  ->", await a.req(c, bytes([LIST])+b"/playlists/9"))
        print("play? ->", await a.req(c, bytes([GET])+b"/playlists/9/play"))
        print("pos0? ->", await a.req(c, bytes([GET])+b"/playlists/9/0"))
        print("DEL   ->", await a.req(c, bytes([DEL])+b"/playlists/9"))
        print("LIST2 ->", await a.req(c, bytes([LIST])+b"/playlists"))

asyncio.run(main())
