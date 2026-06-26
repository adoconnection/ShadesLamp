import { create } from 'zustand';
import { persist, createJSONStorage } from 'zustand/middleware';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { FavoriteVariant } from '../types/favorites';

// Legacy store, kept ONLY so the playlist migration can read pre-playlist
// favorites once and move them onto the lamp (then it clears `variants`).
// New code uses usePlaylistStore.
interface FavoritesState {
  variants: FavoriteVariant[];
}

export const useFavoritesStore = create<FavoritesState>()(
  persist(
    () => ({ variants: [] as FavoriteVariant[] }),
    {
      name: '@favorites',
      version: 2,
      storage: createJSONStorage(() => AsyncStorage),
      migrate: (persisted: any, version: number) => {
        if (version < 2) {
          const ids: number[] = Array.isArray(persisted?.favorites) ? persisted.favorites : [];
          return {
            variants: ids.map((id, i) => ({
              key: `legacy-${id}`, programId: id, name: '', params: [], createdAt: i,
            })),
          };
        }
        return persisted;
      },
      partialize: (s) => ({ variants: s.variants }),
    },
  ),
);
