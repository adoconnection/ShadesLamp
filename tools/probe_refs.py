"""Show which program ids are referenced by playlists / order / active program."""
import asyncio, json, sys
from bleak import BleakScanner, BleakClient

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"
CMD = "0000ff01-0000-1000-8000-00805f9b34fb"
RSP = "0000ff02-0000-1000-8000-00805f9b34fb"
ACT = "0000ff03-0000-1000-8000-00805f9b34fb"

CMD_PL_LIST = 0x32
CMD_PL_GET = 0x33
CMD_GET_ORDER = 0x2B


class Asm:
    def __init__(self): self.buf=bytearray(); self.future=None; self.error=False
    def handle(self,_c,d):
        if len(d)<2: return
        self.buf.extend(d[2:])
        if d[1]&0x02: self.error=True
        if d[1]&0x01 and self.future and not self.future.done(): self.future.set_result(bytes(self.buf))
    async def req(self,c,p,t=15.0):
        loop=asyncio.get_running_loop(); self.buf=bytearray(); self.error=False
        self.future=loop.create_future()
        await c.write_gatt_char(CMD,p,response=True)
        raw=await asyncio.wait_for(self.future,t)
        return raw.decode("utf-8","replace"),self.error


async def main():
    try: sys.stdout.reconfigure(encoding="utf-8",errors="replace")
    except Exception: pass
    dev=await BleakScanner.find_device_by_filter(lambda d,ad: SVC in (ad.service_uuids or []),timeout=15.0)
    if not dev: print("lamp not found",file=sys.stderr); sys.exit(2)
    async with BleakClient(dev) as c:
        a=Asm(); await c.start_notify(RSP,a.handle)
        active=await c.read_gatt_char(ACT)
        print("ACTIVE program id =", active[0] if active else "-")
        order,_=await a.req(c,bytes([CMD_GET_ORDER])); print("ORDER ->",order)
        pls,_=await a.req(c,bytes([CMD_PL_LIST]))
        for p in json.loads(pls):
            full,_=await a.req(c,bytes([CMD_PL_GET,p['id']&0xFF]))
            d=json.loads(full)
            progs=[(i,pos.get('prog'),pos.get('name')) for i,pos in enumerate(d.get('positions',[]))]
            print(f"\nPlaylist {p['id']} {d.get('name')!r}: prog ids used =",
                  sorted({pp for _,pp,_ in progs}))
            for i,pp,nm in progs:
                print(f"    pos[{i}] prog={pp} {nm!r}")

asyncio.run(main())
