"""Export playlists from the lamp and stamp a stable `guid` onto every position.

Resolution is SLUG-FIRST: a position's stored `slug` is the reliable snapshot of
which program it is. Numeric `prog` slots drift as programs are deleted/re-added,
so prog->guid can point at the wrong program. We therefore resolve:
    1) existing guid (kept as-is),
    2) slug -> guid via the repo's programs/<slug>/meta.json, stamped only if that
       guid is actually installed on the lamp right now,
    3) slug absent -> prog -> the lamp's current guid at that slot (last resort).
A position we can't confidently resolve is left legacy (no guid); the app shows it
as "missing" and the lamp skips it during playback.

Phases:
  python migrate_playlists.py export    # device -> pristine backup + fixed copies
  python migrate_playlists.py rebuild   # local backup -> fixed (re-resolve, no re-download)
  python migrate_playlists.py upload    # write fixed copies back to the lamp

Backups: tools/pl_backup/ (raw, pristine).  Fixed: tools/pl_fixed/.
Fixed files keep prog/slug/name so they stay readable by older firmware.
"""
import asyncio, glob, json, os, sys
from bleak import BleakScanner, BleakClient

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

SVC = "0000ff00-0000-1000-8000-00805f9b34fb"
CMD = "0000ff01-0000-1000-8000-00805f9b34fb"
RSP = "0000ff02-0000-1000-8000-00805f9b34fb"
GET_PROGRAMS = 0x01
GET_FILE     = 0x2D
WRITE_FILE   = 0x2F
APPEND_FILE  = 0x31
PL_LIST      = 0x32
PL_STOP      = 0x3D

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BACKUP = os.path.join(HERE, "pl_backup")
FIXED  = os.path.join(HERE, "pl_fixed")
FILE_CHUNK = 180


class Conn:
    def __init__(self): self.buf = bytearray(); self.fut = None
    def on(self, _c, d):
        if len(d) < 2: return
        self.buf.extend(d[2:])
        if d[1] & 0x01 and self.fut and not self.fut.done():
            self.fut.set_result(bytes(self.buf))
    async def req(self, c, payload, to=15):
        self.buf = bytearray(); self.fut = asyncio.get_running_loop().create_future()
        await c.write_gatt_char(CMD, payload, response=True)
        return await asyncio.wait_for(self.fut, to)
    async def text(self, c, payload, to=15):
        return (await self.req(c, payload, to)).decode("utf-8", "replace")


async def find():
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SVC in (ad.service_uuids or []), timeout=15.0)
    if not dev:
        print("lamp not found", file=sys.stderr); sys.exit(2)
    return dev


def repo_slug2guid():
    m = {}
    for p in glob.glob(os.path.join(REPO, "programs", "*", "meta.json")):
        slug = os.path.basename(os.path.dirname(p))
        try:
            g = json.load(open(p, encoding="utf-8")).get("guid")
        except Exception:
            continue
        if g:
            m[slug] = g
    return m


async def lamp_maps(c, a):
    """Return (prog2guid, installed_guids) from the lamp's program list."""
    progs = json.loads(await a.text(c, bytes([GET_PROGRAMS])))
    prog2guid = {p["id"]: p["guid"] for p in progs if p.get("guid")}
    installed = set(prog2guid.values())
    return prog2guid, installed, len(progs)


def stamp(playlist, slug2guid, prog2guid, installed):
    changes = []
    positions = playlist.get("positions") or []
    for i, pos in enumerate(positions):
        if pos.get("guid"):
            changes.append(f"  pos[{i}] already has guid"); continue
        slug = pos.get("slug"); prog = pos.get("prog"); name = pos.get("name")
        guid = None; how = None
        g = slug2guid.get(slug)
        if g and g in installed:
            guid, how = g, f"slug={slug}"
        elif slug and (slug2guid.get(slug) not in installed):
            # slug known but its program isn't on the lamp -> genuinely missing
            changes.append(f"  pos[{i}] slug={slug} ({name}) -> MISSING on lamp, left legacy")
            continue
        elif prog in prog2guid:
            guid, how = prog2guid[prog], f"prog={prog} (no slug)"
        if guid is None:
            changes.append(f"  pos[{i}] prog={prog} slug={slug} ({name}) -> UNRESOLVED, left legacy")
            continue
        newpos = {"guid": guid}; newpos.update(pos)
        positions[i] = newpos
        changes.append(f"  pos[{i}] {how} ({name}) -> {guid}")
    playlist["positions"] = positions
    return playlist, changes


def write_fixed(pid, playlist):
    os.makedirs(FIXED, exist_ok=True)
    with open(os.path.join(FIXED, f"{pid}.json"), "w", encoding="utf-8") as f:
        json.dump(playlist, f, ensure_ascii=False, separators=(",", ":"))


async def export_cmd():
    os.makedirs(BACKUP, exist_ok=True)
    dev = await find()
    async with BleakClient(dev) as c:
        a = Conn(); await c.start_notify(RSP, a.on)
        slug2guid = repo_slug2guid()
        prog2guid, installed, n = await lamp_maps(c, a)
        print(f"programs on lamp: {n} (with guid: {len(prog2guid)}), repo slugs: {len(slug2guid)}")
        summary = json.loads(await a.text(c, bytes([PL_LIST])))
        ids = [s["id"] for s in summary]
        print(f"playlists: {ids}\n")
        for pid in ids:
            raw = (await a.req(c, bytes([GET_FILE]) + f"/playlists/{pid}.json".encode())).decode("utf-8", "replace")
            with open(os.path.join(BACKUP, f"{pid}.json"), "w", encoding="utf-8") as f:
                f.write(raw)
            pl = json.loads(raw)
            fixed, changes = stamp(pl, slug2guid, prog2guid, installed)
            write_fixed(pid, fixed)
            print(f"playlist {pid} '{pl.get('name')}':"); print("\n".join(changes) or "  (empty)"); print()
    print(f"raw backups  -> {BACKUP}\nfixed copies -> {FIXED}")


async def rebuild_cmd():
    files = sorted(glob.glob(os.path.join(BACKUP, "*.json")))
    if not files:
        print("no backups; run 'export' first", file=sys.stderr); sys.exit(2)
    dev = await find()
    async with BleakClient(dev) as c:
        a = Conn(); await c.start_notify(RSP, a.on)
        slug2guid = repo_slug2guid()
        prog2guid, installed, n = await lamp_maps(c, a)
        print(f"programs on lamp: {n} (with guid: {len(prog2guid)}), repo slugs: {len(slug2guid)}\n")
    for fp in files:
        pid = os.path.basename(fp)[:-5]
        pl = json.load(open(fp, encoding="utf-8"))
        fixed, changes = stamp(pl, slug2guid, prog2guid, installed)
        write_fixed(pid, fixed)
        print(f"playlist {pid} '{pl.get('name')}':"); print("\n".join(changes) or "  (empty)"); print()
    print(f"fixed copies -> {FIXED}")


async def write_file(c, a, path, content):
    data = content.encode("utf-8"); pb = path.encode("utf-8")
    off = 0; first = True
    while off < len(data) or first:
        chunk = data[off:off + FILE_CHUNK]
        cmd = WRITE_FILE if first else APPEND_FILE
        await a.req(c, bytes([cmd, len(pb)]) + pb + chunk)
        off += FILE_CHUNK; first = False
        if off >= len(data): break


async def upload_cmd():
    files = sorted(glob.glob(os.path.join(FIXED, "*.json")))
    if not files:
        print("no fixed copies; run 'export'/'rebuild' first", file=sys.stderr); sys.exit(2)
    dev = await find()
    async with BleakClient(dev) as c:
        a = Conn(); await c.start_notify(RSP, a.on)
        await a.req(c, bytes([PL_STOP]))
        for fp in files:
            pid = os.path.basename(fp)[:-5]
            content = open(fp, encoding="utf-8").read()
            await write_file(c, a, f"/playlists/{pid}.json", content)
            back = (await a.req(c, bytes([GET_FILE]) + f"/playlists/{pid}.json".encode())).decode("utf-8", "replace")
            ok = back.strip() == content.strip()
            print(f"uploaded /playlists/{pid}.json ({len(content)} B) verify={'OK' if ok else 'MISMATCH'}")
    print("done")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "export"
    fns = {"export": export_cmd, "rebuild": rebuild_cmd, "upload": upload_cmd}
    if cmd not in fns:
        print("usage: migrate_playlists.py [export|rebuild|upload]"); sys.exit(1)
    asyncio.run(fns[cmd]())


if __name__ == "__main__":
    main()
