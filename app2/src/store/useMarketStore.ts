import { create } from 'zustand';
import { MarketItem } from '../types/marketplace';

const BASE_URL = 'https://raw.githubusercontent.com/adoconnection/ShadesLamp/refs/heads/main';
const INDEX_URL = `${BASE_URL}/programs/index.json`;

interface IndexEntry {
  slug: string;
  meta: string;
  wasm?: string;
}

interface MarketState {
  catalog: MarketItem[];
  featured: string[];
  installedSlugs: string[];
  loading: boolean;
  error: string | null;
  fetchCatalog: () => Promise<void>;
  markInstalled: (slug: string) => void;
  unmarkInstalled: (slug: string) => void;
  setInstalledSlugs: (slugs: string[]) => void;
  isInstalled: (slug: string) => boolean;
}

export const useMarketStore = create<MarketState>((set, get) => ({
  catalog: [],
  featured: [],
  installedSlugs: [],
  loading: false,
  error: null,

  fetchCatalog: async () => {
    set({ loading: true, error: null });
    try {
      const indexRes = await fetch(INDEX_URL);
      if (!indexRes.ok) throw new Error(`Index fetch failed: ${indexRes.status}`);
      const index = await indexRes.json();
      const entries: IndexEntry[] = index.programs || [];
      const featuredSlugs: string[] = index.featured || [];

      // Fetch all meta.json in parallel
      const items = await Promise.all(
        entries
          .filter((e) => e.wasm) // Only show programs that have wasm
          .map(async (entry): Promise<MarketItem | null> => {
            try {
              const metaRes = await fetch(`${BASE_URL}/${entry.meta}`);
              if (!metaRes.ok) return null;
              const meta = await metaRes.json();
              return {
                slug: entry.slug,
                name: meta.name || entry.slug,
                author: meta.author || 'unknown',
                desc: meta.desc || '',
                category: meta.category || 'Effects',
                cover: meta.cover || { from: '#333', to: '#666', angle: 135 },
                version: meta.version,
                coverSvg: meta.coverSvg,
                pulse: meta.pulse || '#FFFFFF',
                tags: meta.tags || [],
                wasmUrl: `${BASE_URL}/${entry.wasm}`,
                metaUrl: `${BASE_URL}/${entry.meta}`,
                i18n: meta.i18n,
              };
            } catch {
              return null;
            }
          }),
      );

      set({ catalog: items.filter(Boolean) as MarketItem[], featured: featuredSlugs, loading: false });
    } catch (err: any) {
      set({ error: err.message || 'Failed to load catalog', loading: false });
    }
  },

  markInstalled: (slug) =>
    set((state) => ({
      installedSlugs: [...new Set([...state.installedSlugs, slug])],
    })),

  unmarkInstalled: (slug) =>
    set((state) => ({
      installedSlugs: state.installedSlugs.filter((s) => s !== slug),
    })),

  setInstalledSlugs: (slugs) =>
    set({ installedSlugs: [...new Set(slugs)] }),

  isInstalled: (slug) => get().installedSlugs.includes(slug),
}));
