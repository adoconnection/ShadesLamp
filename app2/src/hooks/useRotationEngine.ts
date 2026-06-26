import { useEffect } from 'react';
import { usePlaylistStore } from '../store/usePlaylistStore';
import { useBleStore } from '../store/useBleStore';
import { applyPosition } from '../ble/applyPosition';

// Drives playlist playback while one is playing and the lamp is connected.
// The playing playlist's rotation mode governs auto-advance: 'off' stays on the
// current position, 'next' steps sequentially, 'random' jumps around. Position
// switches go through applyPosition -> setActiveProgram, which fades on-device.
export function useRotationEngine() {
  const playingId = usePlaylistStore((s) => s.playingId);
  const connectionState = useBleStore((s) => s.connectionState);
  const playing = usePlaylistStore((s) => s.playlists.find((p) => p.id === s.playingId));
  const mode = playing?.mode;
  const interval = playing?.interval;

  useEffect(() => {
    if (playingId == null || mode == null || mode === 'off') return;
    if (connectionState !== 'connected') return;

    const tick = () => {
      const st = usePlaylistStore.getState();
      const pl = st.playlists.find((p) => p.id === st.playingId);
      if (!pl || pl.positions.length === 0) return;

      let i = st.currentIndex ?? 0;
      if (i < 0 || i >= pl.positions.length) i = 0;
      let next: number;
      if (pl.mode === 'random' && pl.positions.length > 1) {
        do { next = Math.floor(Math.random() * pl.positions.length); } while (next === i);
      } else {
        next = (i + 1) % pl.positions.length;
      }
      st.setCurrentIndex(next);
      applyPosition(pl.positions[next]);
    };

    const id = setInterval(tick, Math.max(2, interval || 30) * 1000);
    return () => clearInterval(id);
  }, [playingId, mode, interval, connectionState]);
}
