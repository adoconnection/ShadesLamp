import { plList, plGet } from './commands';
import { Playlist, PlaylistPosition, RotationMode } from '../types/playlist';
import { Program } from '../types/program';

export const NUM_TO_MODE: RotationMode[] = ['off', 'next', 'random'];
export const MODE_TO_NUM: Record<RotationMode, number> = { off: 0, next: 1, random: 2 };

const byName = (a: Playlist, b: Playlist) =>
  a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) || a.id - b.id;

let uidCounter = 0;
export function newUid(): string { return `pos-${Date.now().toString(36)}-${uidCounter++}`; }

function parsePositions(raw: any): PlaylistPosition[] {
  if (!Array.isArray(raw)) return [];
  return raw.map((p: any) => ({
    uid: newUid(),
    guid: typeof p.guid === 'string' ? p.guid : undefined,
    prog: typeof p.prog === 'number' ? p.prog : 0,
    slug: p.slug,
    name: p.name,
    params: Array.isArray(p.params) ? p.params : [],
  }));
}

// Resolve a playlist position to its current program, or undefined if the
// program is no longer installed (deleted / not re-downloaded yet). Matches by
// the stable guid first; a guid'd position that doesn't resolve is "missing"
// (we deliberately do NOT fall back to the numeric id, which may now point at a
// different program). Legacy positions without a guid fall back to slug → id.
export function findProgramForPosition(
  pos: PlaylistPosition,
  programs: Program[],
): Program | undefined {
  if (pos.guid) return programs.find((p) => p.guid === pos.guid);
  if (pos.slug) {
    const bySlug = programs.find((p) => p.slug === pos.slug);
    if (bySlug) return bySlug;
  }
  return programs.find((p) => p.id === pos.prog);
}

// Load all playlists from the lamp via the semantic commands.
export async function loadPlaylists(): Promise<Playlist[]> {
  let summary: any;
  try { summary = await plList(); } catch { return []; }
  if (!Array.isArray(summary)) return [];

  const out: Playlist[] = [];
  for (const s of summary) {
    if (typeof s?.id !== 'number') continue;
    let full: any;
    try { full = await plGet(s.id); } catch { full = null; }
    if (full && typeof full === 'object' && Array.isArray(full.positions)) {
      out.push({
        id: s.id,
        name: full.name || `Playlist ${s.id}`,
        mode: NUM_TO_MODE[full.mode ?? 0] || 'off',
        interval: typeof full.interval === 'number' ? full.interval : 30,
        positions: parsePositions(full.positions),
      });
    } else {
      out.push({
        id: s.id,
        name: s.name || `Playlist ${s.id}`,
        mode: NUM_TO_MODE[s.mode ?? 0] || 'off',
        interval: typeof s.interval === 'number' ? s.interval : 30,
        positions: [],
      });
    }
  }
  out.sort(byName);
  return out;
}

// Serialize a position the way the firmware stores it.
export function positionJson(pos: PlaylistPosition): string {
  return JSON.stringify({ guid: pos.guid, prog: pos.prog, slug: pos.slug, name: pos.name, params: pos.params });
}
