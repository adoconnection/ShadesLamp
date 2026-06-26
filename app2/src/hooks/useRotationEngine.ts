import { useEffect, useRef } from 'react';
import { useFavoritesStore } from '../store/useFavoritesStore';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { buildFavoriteList } from '../utils/favorites';
import { applyVariant } from '../ble/applyVariant';

// Drives favorite rotation while the app is foregrounded and connected. When
// rotationMode is 'next' it steps through the sorted favorites list; 'random'
// picks a different variant each tick. Runs app-wide (mounted at the root) so
// rotation keeps going after you leave the Favorites screen.
export function useRotationEngine() {
  const rotationMode = useFavoritesStore((s) => s.rotationMode);
  const intervalSec = useFavoritesStore((s) => s.rotationIntervalSec);
  const connectionState = useBleStore((s) => s.connectionState);
  const idxRef = useRef(0);

  useEffect(() => {
    if (rotationMode === 'off') return;
    if (connectionState !== 'connected') return;

    const tick = () => {
      const variants = useFavoritesStore.getState().variants;
      if (variants.length === 0) return;
      const order = buildFavoriteList(variants, useProgramStore.getState().programs);
      if (order.length === 0) return;

      let next: number;
      if (rotationMode === 'random' && order.length > 1) {
        do {
          next = Math.floor(Math.random() * order.length);
        } while (next === idxRef.current);
      } else {
        next = (idxRef.current + 1) % order.length;
      }
      idxRef.current = next;
      applyVariant(order[next].v);
    };

    const id = setInterval(tick, Math.max(2, intervalSec) * 1000);
    return () => clearInterval(id);
  }, [rotationMode, intervalSec, connectionState]);
}
