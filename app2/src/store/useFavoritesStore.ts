import { create } from 'zustand';
import { persist, createJSONStorage } from 'zustand/middleware';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { FavoriteVariant, RotationMode } from '../types/favorites';

interface FavoritesState {
  variants: FavoriteVariant[];
  rotationMode: RotationMode;
  rotationIntervalSec: number;
  addVariant: (v: Omit<FavoriteVariant, 'key' | 'createdAt'>) => void;
  removeVariant: (key: string) => void;
  setRotationMode: (mode: RotationMode) => void;
  setRotationIntervalSec: (sec: number) => void;
  // True if any saved variant belongs to this program (matched by slug, else id).
  hasProgram: (match: { slug?: string; programId: number }) => boolean;
}

function genKey(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

export const useFavoritesStore = create<FavoritesState>()(
  persist(
    (set, get) => ({
      variants: [],
      rotationMode: 'off',
      rotationIntervalSec: 30,

      addVariant: (v) =>
        set((s) => ({
          variants: [...s.variants, { ...v, key: genKey(), createdAt: Date.now() }],
        })),

      removeVariant: (key) =>
        set((s) => ({ variants: s.variants.filter((x) => x.key !== key) })),

      setRotationMode: (rotationMode) => set({ rotationMode }),
      setRotationIntervalSec: (rotationIntervalSec) => set({ rotationIntervalSec }),

      hasProgram: ({ slug, programId }) =>
        get().variants.some((x) =>
          slug && x.slug ? x.slug === slug : x.programId === programId,
        ),
    }),
    {
      name: '@favorites',
      version: 2,
      storage: createJSONStorage(() => AsyncStorage),
      // v1 stored { favorites: number[] } (program ids, no params). Convert each
      // to a paramless variant; the screen resolves name/cover from the live
      // program list by id, and applying it just activates the program.
      migrate: (persisted: any, version: number) => {
        if (version < 2) {
          const ids: number[] = Array.isArray(persisted?.favorites) ? persisted.favorites : [];
          return {
            variants: ids.map((id, i) => ({
              key: `legacy-${id}`,
              programId: id,
              name: '',
              params: [],
              createdAt: i,
            })),
            rotationMode: 'off' as RotationMode,
            rotationIntervalSec: 30,
          };
        }
        return persisted;
      },
      partialize: (s) => ({
        variants: s.variants,
        rotationMode: s.rotationMode,
        rotationIntervalSec: s.rotationIntervalSec,
      }),
    },
  ),
);
