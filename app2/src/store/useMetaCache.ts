import AsyncStorage from '@react-native-async-storage/async-storage';
import { Gradient } from '../types/program';

const CACHE_KEY = '@metaCache';
const MAX_ENTRIES = 200;

export interface CachedMeta {
  name: string;
  desc: string;
  author: string;
  category: string;
  cover: Gradient;
  coverSvg?: string;
  pulse: string;
  tags?: string[];
  slug?: string;
}

type MetaCache = Record<string, CachedMeta>;

let memoryCache: MetaCache | null = null;

async function loadCache(): Promise<MetaCache> {
  if (memoryCache) return memoryCache;
  try {
    const raw = await AsyncStorage.getItem(CACHE_KEY);
    memoryCache = raw ? JSON.parse(raw) : {};
  } catch {
    memoryCache = {};
  }
  return memoryCache!;
}

async function saveCache(cache: MetaCache): Promise<void> {
  memoryCache = cache;
  // Prune if too large
  const keys = Object.keys(cache);
  if (keys.length > MAX_ENTRIES) {
    const toRemove = keys.slice(0, keys.length - MAX_ENTRIES);
    for (const k of toRemove) delete cache[k];
  }
  await AsyncStorage.setItem(CACHE_KEY, JSON.stringify(cache));
}

export function cacheKey(guid: string, version: string): string {
  return `${guid}:${version}`;
}

export async function getCachedMeta(guid: string, version: string): Promise<CachedMeta | null> {
  const cache = await loadCache();
  return cache[cacheKey(guid, version)] ?? null;
}

export async function setCachedMeta(guid: string, version: string, meta: CachedMeta): Promise<void> {
  const cache = await loadCache();
  cache[cacheKey(guid, version)] = meta;
  await saveCache(cache);
}
