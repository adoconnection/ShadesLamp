import { create } from 'zustand';
import { Playlist, PlaylistPosition, RotationMode } from '../types/playlist';
import { loadPlaylists, positionJson, newUid, MODE_TO_NUM } from '../ble/playlists';
import {
  plCreate, plRename, plDelete, plSetRotation, plAddPosition, plRemovePosition, plReorder,
  plSetPosParams, plPlay, plStop, plGetState,
} from '../ble/commands';
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
  updatePositionParams: (id: number, index: number, params: PlaylistPosition['params']) => Promise<void>;
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

    // The lamp owns rotation — sync which playlist (if any) it is already
    // playing, so the UI reflects on-device state after a reconnect.
    let playingId: number | null = null;
    let currentIndex: number | null = null;
    try {
      const st = await plGetState();
      if (st && typeof st.playing === 'number' && st.playing >= 0) {
        playingId = st.playing;
        currentIndex = typeof st.index === 'number' ? st.index : 0;
      }
    } catch { /* leave idle */ }

    set({ playlists: lists, loaded: true, playingId, currentIndex });
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

  updatePositionParams: async (id, index, params) => {
    if (!connected()) return;
    set({
      playlists: get().playlists.map((p) =>
        p.id === id
          ? { ...p, positions: p.positions.map((pos, i) => (i === index ? { ...pos, params } : pos)) }
          : p),
    });
    await plSetPosParams(id, index, params).catch(() => {});
  },

  reorder: async (id, newPositions) => {
    if (!connected()) return;
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl) return;
    // Map each dragged item back to its original index. Match by uid (stable per
    // session) so it works even if the drag list hands back cloned objects;
    // fall back to reference identity when a uid is somehow missing.
    const indices = newPositions
      .map((np) =>
        np.uid != null ? pl.positions.findIndex((o) => o.uid === np.uid) : pl.positions.indexOf(np))
      .filter((i) => i >= 0);
    // Only persist a full, valid permutation; a short list would tell the lamp
    // to drop positions.
    if (indices.length !== pl.positions.length) return;
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, positions: newPositions } : p)) });
    await plReorder(id, indices).catch(() => {});
  },

  setRotation: async (id, mode, interval) => {
    if (!connected()) return;
    set({ playlists: get().playlists.map((p) => (p.id === id ? { ...p, mode, interval } : p)) });
    await plSetRotation(id, MODE_TO_NUM[mode], interval).catch(() => {});
  },

  // Playback is driven by the lamp. The store only sends a semantic command and
  // mirrors the resulting state locally for the UI; the lamp does the rotation.
  play: (id) => {
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl || pl.positions.length === 0) { set({ playingId: id, currentIndex: null }); return; }
    set({ playingId: id, currentIndex: 0 });
    if (connected()) plPlay(id, 0).catch(() => {});
  },

  playPosition: (id, index) => {
    const pl = get().playlists.find((p) => p.id === id);
    if (!pl || index < 0 || index >= pl.positions.length) return;
    set({ playingId: id, currentIndex: index });
    if (connected()) plPlay(id, index).catch(() => {});
  },

  stop: () => {
    if (get().playingId == null) return;
    set({ playingId: null });
    if (connected()) plStop().catch(() => {});
  },

  // Manual swipe: jump to the neighbouring position and let the lamp keep
  // rotating from there (PL_PLAY resets the interval at the new index).
  advance: (dir) => {
    const { playingId, currentIndex, playlists } = get();
    if (playingId == null) return;
    const pl = playlists.find((p) => p.id === playingId);
    if (!pl || pl.positions.length === 0) return;
    let i = currentIndex ?? 0;
    if (i < 0 || i >= pl.positions.length) i = 0;
    const ni = (i + dir + pl.positions.length) % pl.positions.length;
    set({ currentIndex: ni });
    if (connected()) plPlay(playingId, ni).catch(() => {});
  },

  setCurrentIndex: (currentIndex) => set({ currentIndex }),
}));
