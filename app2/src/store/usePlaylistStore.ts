import { create } from 'zustand';
import { Playlist, PlaylistPosition, RotationMode } from '../types/playlist';
import { loadPlaylists, positionJson, newUid, MODE_TO_NUM } from '../ble/playlists';
import {
  plCreate, plRename, plDelete, plSetRotation, plAddPosition, plRemovePosition, plReorder,
} from '../ble/commands';
import { applyPosition } from '../ble/applyPosition';
import { useBleStore } from './useBleStore';
import { useFavoritesStore } from './useFavoritesStore';

interface PlaylistState {
  playlists: Playlist[];
  loaded: boolean;
  playingId: number | null;
  currentIndex: number | null;

  load: () => Promise<void>;
  createPlaylist: (name: string) => Promise<number | null>;
  renamePlaylist: (id: number, name: string) => Promise<void>;
  deletePlaylist: (id: number) => Promise<void>;
  addPosition: (id: number, pos: PlaylistPosition) => Promise<void>;
  removePosition: (id: number, index: number) => Promise<void>;
  reorder: (id: number, newPositions: PlaylistPosition[]) => Promise<void>;
  setRotation: (id: number, mode: RotationMode, interval: number) => Promise<void>;

  play: (id: number) => void;
  playPosition: (id: number, index: number) => void;
  stop: () => void;
  advance: (dir: number) => void;
  setCurrentIndex: (i: number | null) => void;
}

const connected = () => useBleStore.getState().connectionState === 'connected';
const byName = (a: Playlist, b: Playlist) =>
  a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) || a.id - b.id;

export const usePlaylistStore = create<PlaylistState>((set, get) => ({
  playlists: [],
  loaded: false,
  playingId: null,
  currentIndex: null,

  load: async () => {
    if (!connected()) return;
    let lists = await loadPlaylists();

    // One-shot migration of legacy local favorites into a lamp playlist.
    if (lists.length === 0) {
      const variants = useFavoritesStore.getState().variants;
      if (variants.length > 0) {
        const res = await plCreate('Избранное').catch(() => null);
        const id = res && res.ok ? res.id : null;
        if (id != null) {
          for (const v of variants) {
            const pos: PlaylistPosition = {
              prog: v.programId, slug: v.slug, name: v.name,
              params: v.params.map((p) => ({ id: p.id, value: p.value, f: p.isFloat })),
            };
            await plAddPosition(id, positionJson(pos)).catch(() => {});
          }
          useFavoritesStore.setState({ variants: [] });
          lists = await loadPlaylists();
        }
      }
    }

    set({ playlists: lists, loaded: true });
  },

  createPlaylist: async (name) => {
    if (!connected()) return null;
    const res = await plCreate(name).catch(() => null);
    if (!res || !res.ok) return null;
    const pl: Playlist = { id: res.id, name, mode: 'off', interval: 30, positions: [] };
    set({ playlists: [...get().playlists, pl].sort(byName) });
    return res.id;
  },

  renamePlaylist: async (id, name) => {
    if (!connected()) return;
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, name } : p)).sort(byName) });
    await plRename(id, name).catch(() => {});
  },

  deletePlaylist: async (id) => {
    if (!connected()) return;
    set((s) => ({
      playlists: s.playlists.filter((p) => p.id !== id),
      playingId: s.playingId === id ? null : s.playingId,
    }));
    await plDelete(id).catch(() => {});
  },

  addPosition: async (id, pos) => {
    if (!connected()) return;
    const withUid = { ...pos, uid: pos.uid || newUid() };
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, positions: [...p.positions, withUid] } : p)) });
    await plAddPosition(id, positionJson(withUid)).catch(() => {});
  },

  removePosition: async (id, index) => {
    if (!connected()) return;
    set({
      playlists: get().playlists.map((p) =>
        p.id === id ? { ...p, positions: p.positions.filter((_, i) => i !== index) } : p),
    });
    await plRemovePosition(id, index).catch(() => {});
  },

  reorder: async (id, newPositions) => {
    if (!connected()) return;
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl) return;
    const indices = newPositions.map((np) => pl.positions.indexOf(np)).filter((i) => i >= 0);
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, positions: newPositions } : p)) });
    await plReorder(id, indices).catch(() => {});
  },

  setRotation: async (id, mode, interval) => {
    if (!connected()) return;
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, mode, interval } : p)) });
    await plSetRotation(id, MODE_TO_NUM[mode], interval).catch(() => {});
  },

  play: (id) => {
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl || pl.positions.length === 0) { set({ playingId: id, currentIndex: null }); return; }
    set({ playingId: id, currentIndex: 0 });
    applyPosition(pl.positions[0]);
  },

  playPosition: (id, index) => {
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl || index < 0 || index >= pl.positions.length) return;
    set({ playingId: id, currentIndex: index });
    applyPosition(pl.positions[index]);
  },

  stop: () => set({ playingId: null }),

  advance: (dir) => {
    const { playingId, currentIndex, playlists } = get();
    if (playingId == null) return;
    const pl = playlists.find((p) => p.id === playingId);
    if (!pl || pl.positions.length === 0) return;
    let i = currentIndex ?? 0;
    if (i < 0 || i >= pl.positions.length) i = 0;
    const ni = (i + dir + pl.positions.length) % pl.positions.length;
    set({ currentIndex: ni });
    applyPosition(pl.positions[ni]);
  },

  setCurrentIndex: (currentIndex) => set({ currentIndex }),
}));
