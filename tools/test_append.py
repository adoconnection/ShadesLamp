"""Test chunked file write (WRITE_FILE first chunk + APPEND_FILE rest)."""
import asyncio, sys
from bleak import BleakScanner, BleakClient
SVC="0000ff00-0000-1000-8000-00805f9b34fb"
CMD="0000ff01-0000-1000-8000-00805f9b34fb"; RSP="0000ff02-0000-1000-8000-00805f9b34fb"
WRITE,APPEND,GET,DEL=0x2F,0x31,0x2D,0x30
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
async def main():
    dev=await BleakScanner.find_device_by_filter(lambda d,ad: SVC in (ad.service_uuids or []),timeout=15)
    if not dev: print("no lamp"); sys.exit(2)
    path=b"/playlists/test/big"
    content=('{"order":['+",".join('"%d"'%i for i in range(90))+']}')  # ~ large
    data=content.encode()
    print("size", len(data))
    CH=200
    async with BleakClient(dev) as c:
        a=A(); await c.start_notify(RSP,a.h)
        off=0; first=True
        while off<len(data):
            chunk=data[off:off+CH]
            cmd=WRITE if first else APPEND
            await a.req(c, bytes([cmd,len(path)])+path+chunk)
            off+=CH; first=False
        got=await a.req(c, bytes([GET])+path, to=12)
        print("match", got==content, "len", len(got))
        await a.req(c, bytes([DEL])+b"/playlists/test")
asyncio.run(main())
