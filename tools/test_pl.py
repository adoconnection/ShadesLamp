"""Exercise the semantic playlist BLE commands (CMD_PL_*)."""
import asyncio, sys, struct
from bleak import BleakScanner, BleakClient
SVC="0000ff00-0000-1000-8000-00805f9b34fb"
CMD="0000ff01-0000-1000-8000-00805f9b34fb"; RSP="0000ff02-0000-1000-8000-00805f9b34fb"
LIST,GET,CREATE,RENAME,DELETE,ROT,ADD,DELP,REORDER=0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A
HW=0x22
class A:
    def __init__(s): s.buf=bytearray(); s.fut=None
    def h(s,_c,d):
        if len(d)<2: return
        s.buf.extend(d[2:])
        if d[1]&1 and s.fut and not s.fut.done(): s.fut.set_result(bytes(s.buf))
    async def req(s,c,p,to=10):
        s.buf=bytearray(); s.fut=asyncio.get_running_loop().create_future()
        await c.write_gatt_char(CMD,p,response=True)
        return (await asyncio.wait_for(s.fut,to)).decode("utf-8","replace")
def safe(x): return x.encode("ascii","backslashreplace").decode("ascii")
async def main():
    dev=await BleakScanner.find_device_by_filter(lambda d,ad: SVC in (ad.service_uuids or []),timeout=15)
    if not dev: print("no lamp"); sys.exit(2)
    async with BleakClient(dev) as c:
        a=A(); await c.start_notify(RSP,a.h)
        print("HW    ->", safe(await a.req(c, bytes([HW])))[:60])
        r=await a.req(c, bytes([CREATE])+b"Test PL"); print("CREATE->", r)
        import json; pid=json.loads(r)["id"]
        print("ADD0  ->", await a.req(c, bytes([ADD,pid])+b'{"prog":5,"slug":"flame","name":"Flame","params":[{"id":0,"value":3,"f":false}]}'))
        print("ADD1  ->", await a.req(c, bytes([ADD,pid])+b'{"prog":7,"slug":"warp","name":"Warp","params":[]}'))
        print("ADD2  ->", await a.req(c, bytes([ADD,pid])+b'{"prog":9,"slug":"rain","name":"Rain","params":[]}'))
        print("LIST  ->", safe(await a.req(c, bytes([LIST]))))
        print("GET   ->", safe(await a.req(c, bytes([GET,pid]))))
        print("REORD ->", await a.req(c, bytes([REORDER,pid])+b'[2,0,1]'))
        print("GET2  ->", safe(await a.req(c, bytes([GET,pid]))))
        print("DELP0 ->", await a.req(c, bytes([DELP,pid,0])))
        print("ROT   ->", await a.req(c, bytes([ROT,pid,1])+struct.pack("<H",15)))
        print("RENAME->", await a.req(c, bytes([RENAME,pid])+b"Renamed"))
        print("GET3  ->", safe(await a.req(c, bytes([GET,pid]))))
        print("DELETE->", await a.req(c, bytes([DELETE,pid])))
        print("LIST2 ->", safe(await a.req(c, bytes([LIST]))))
asyncio.run(main())
