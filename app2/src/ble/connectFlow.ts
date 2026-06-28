import AsyncStorage from '@react-native-async-storage/async-storage';
import { Device } from 'react-native-ble-plx';
import { connectToDevice, setOnDisconnected, setOnActiveChanged, setOnEvent } from './manager';
import { getPrograms, getActiveProgram, getHwConfig, getDeviceName, getMeta, getStorage, getPower } from './commands';
import { EVT } from './constants';
import { Program, ProgramListItem } from '../types/program';
import { getCachedMeta, setCachedMeta, invalidateCache, CachedMeta } from '../store/useMetaCache';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { useMarketStore } from '../store/useMarketStore';
import { usePlaylistStore } from '../store/usePlaylistStore';

const LAST_DEVICE_KEY = '@lastDeviceMac';

export async function saveLastDevice(mac: string): Promise<void> {
  await AsyncStorage.setItem(LAST_DEVICE_KEY, mac);
}

export async function getLastDevice(): Promise<string | null> {
  return AsyncStorage.getItem(LAST_DEVICE_KEY);
}

export async function clearLastDevice(): Promise<void> {
  await AsyncStorage.removeItem(LAST_DEVICE_KEY);
}

/**
 * Connect to a device and load all data (programs, hw config, etc).
 * Updates zustand stores directly.
 * Returns the connected Device on success.
 */
export async function connectAndLoadDevice(
  deviceId: string,
  onDisconnected?: () => void,
): Promise<Device> {
  const { setConnectionState, setDeviceId, setDeviceMac, setDeviceInfo, setPowerOn, setSyncProgress, setPlaylistsLoading } = useBleStore.getState();
  const { setPrograms, setActiveId } = useProgramStore.getState();

  setConnectionState('connecting');

  const device = await connectToDevice(deviceId);
  setDeviceId(deviceId);
  setDeviceMac(device.id);

  setOnDisconnected(() => {
    setConnectionState('disconnected');
    // A connection that drops mid-sync must not leave the spinners stuck.
    setSyncProgress(null);
    setPlaylistsLoading(false);
    setOnActiveChanged(null);
    setOnEvent(null);
    onDisconnected?.();
  });

  // Listen for active program changes pushed by firmware
  setOnActiveChanged((id) => {
    useProgramStore.getState().setActiveId(id);
  });

  // Listen for program added/deleted events pushed by firmware
  setOnEvent((eventType, programId) => {
    const store = useProgramStore.getState();
    const marketStore = useMarketStore.getState();
    if (eventType === EVT.PL_ADVANCE) {
      // The lamp auto-advanced the playing playlist; programId carries the new
      // position index. The active-program change arrives separately via the
      // ACTIVE_PROGRAM characteristic.
      usePlaylistStore.getState().setCurrentIndex(programId);
      return;
    }
    if (eventType === EVT.PL_STOPPED) {
      // The lamp left playlist mode on its own (e.g. the hardware touch button
      // selected a program). Clear local playback state only — don't echo a
      // PL_STOP back to the lamp.
      usePlaylistStore.setState({ playingId: null });
      return;
    }
    if (eventType === EVT.PROGRAM_DELETED) {
      // Find slug before removing so we can unmark in marketplace
      const prog = store.programs.find((p) => p.id === programId);
      if (prog?.slug) marketStore.unmarkInstalled(prog.slug);
      store.removeProgram(programId);
    } else if (eventType === EVT.PROGRAM_ADDED) {
      // Fetch meta for the new program and add to store
      getMeta(programId)
        .then((raw) => {
          const name = raw?.name || `Program ${programId}`;
          store.addProgram({
            id: programId,
            guid: raw?.guid,
            name,
            desc: raw?.desc || '',
            author: raw?.author || 'built-in',
            size: '',
            cover: raw?.cover || { from: '#555555', to: '#999999', angle: 135 },
            coverSvg: raw?.coverSvg,
            pulse: raw?.pulse || '#888888',
            category: raw?.category || 'Effects',
            params: [],
            slug: raw?.slug,
            i18n: raw?.i18n,
          });
          if (raw?.slug) marketStore.markInstalled(raw.slug);
        })
        .catch(() => {
          store.addProgram({
            id: programId,
            name: `Program ${programId}`,
            desc: '',
            author: 'built-in',
            size: '',
            cover: { from: '#555555', to: '#999999', angle: 135 },
            pulse: '#888888',
            category: 'Effects',
            params: [],
          });
        });
    }
  });

  // Each call is independently guarded: a single slow/failed request (e.g. a
  // device that hangs on one command) must not abort the whole connection.
  const [programList, activeId, hwConfig, deviceName, storageInfo, powerOn] = await Promise.all([
    getPrograms().catch(() => [] as Array<{ id: number; name: string }>),
    getActiveProgram().catch(() => -1),
    getHwConfig().catch(() => ({})),
    getDeviceName().catch(() => undefined),
    getStorage().catch(() => ({ used: 0, total: 0, free: 0 })),
    getPower().catch(() => true),
  ]);

  // Build the program list from cached metadata first (instant) and surface the
  // UI right away. Programs whose meta isn't cached get a placeholder now and
  // are filled in progressively in the background — so the first connection
  // feels immediate instead of blocking on N serial GET_META round-trips while
  // the lamp's render loop is frozen handling them.
  const { base, missing } = await buildBasePrograms(programList as ProgramListItem[]);

  setPrograms(base);
  setActiveId(activeId);
  setPowerOn(powerOn);

  // Seed marketplace installed state from what we know now; refined once the
  // background meta load completes (some slugs only arrive with the meta).
  const baseSlugs = base.map((p) => p.slug).filter(Boolean) as string[];
  useMarketStore.getState().setInstalledSlugs(baseSlugs);

  const storageUsedKB = Math.round(storageInfo.used / 1024);
  const storageTotalKB = Math.round(storageInfo.total / 1024);
  setDeviceInfo({
    name: deviceName || 'Shades LED Lamp',
    serial: hwConfig.serial || '—',
    mac: device.id,
    firmware: hwConfig.build != null ? `build ${hwConfig.build}` : (hwConfig.firmware || hwConfig.fw || hwConfig.version || '—'),
    matrix: hwConfig.ok ? `${hwConfig.width} × ${hwConfig.height}` : '—',
    storage: { used: storageUsedKB, total: storageTotalKB },
    rssi: device.rssi ?? 0,
    temp: (hwConfig.ok && typeof hwConfig.temp === 'number') ? hwConfig.temp : undefined,
  });

  // Connected as soon as the essentials are in — the UI is now usable while the
  // rest streams in. Persist for auto-reconnect before returning.
  setConnectionState('connected');
  await saveLastDevice(deviceId);

  // Resolve remaining metadata + load playlists in the background. Not awaited:
  // the caller (and the user) shouldn't wait on it.
  void loadRemainingData(missing);

  return device;
}

// Fetch the metadata still missing after the cached-first build, updating the
// store (and the meta cache) one program at a time, and load the lamp's
// playlists. Drives the sync spinners and yields between BLE reads so the lamp
// gets render time instead of stalling under a tight request burst.
async function loadRemainingData(missing: ProgramListItem[]): Promise<void> {
  const { setSyncProgress, setPlaylistsLoading } = useBleStore.getState();
  const store = useProgramStore.getState();

  if (missing.length > 0) {
    setSyncProgress({ done: 0, total: missing.length });
    let done = 0;
    for (const p of missing) {
      // Bail out if the connection dropped mid-sync.
      if (useBleStore.getState().connectionState !== 'connected') {
        setSyncProgress(null);
        return;
      }
      try {
        const raw = await getMeta(p.id);
        if (raw && raw.name) {
          const meta: CachedMeta = {
            name: raw.name,
            desc: raw.desc || '',
            author: raw.author || 'built-in',
            category: raw.category || 'Effects',
            cover: raw.cover || defaultMeta.cover,
            coverSvg: raw.coverSvg,
            pulse: raw.pulse || '#888888',
            tags: raw.tags,
            slug: raw.slug,
            i18n: raw.i18n,
          };
          store.addProgram(toProgram(p, meta));
          if (p.guid && p.version) await setCachedMeta(p.guid, p.version, meta);
        }
      } catch {
        // Leave the placeholder in place.
      }
      done++;
      setSyncProgress({ done, total: missing.length });
      // Give the lamp's render task a slice between meta reads.
      await new Promise((r) => setTimeout(r, 30));
    }
    setSyncProgress(null);
  }

  // Refresh installed slugs from the now-complete program list.
  const slugs = useProgramStore.getState().programs.map((p) => p.slug).filter(Boolean) as string[];
  useMarketStore.getState().setInstalledSlugs(slugs);

  // Load playlists stored on the lamp (best-effort).
  if (useBleStore.getState().connectionState === 'connected') {
    setPlaylistsLoading(true);
    try {
      await usePlaylistStore.getState().load();
    } catch {
      // ignore
    } finally {
      setPlaylistsLoading(false);
    }
  }
}

// Build a Program from a list entry + resolved (or cached) metadata.
function toProgram(p: ProgramListItem, meta: CachedMeta): Program {
  return {
    id: p.id, guid: p.guid, name: meta.name, desc: meta.desc, author: meta.author,
    size: '', version: p.version, cover: meta.cover, coverSvg: meta.coverSvg,
    pulse: meta.pulse, category: meta.category, params: [], slug: meta.slug,
    i18n: meta.i18n,
  };
}

// Split the program list into ready-to-show entries (cached meta) and entries
// whose meta still needs fetching. Cached entries are built immediately; the
// rest get a placeholder Program so the row shows right away.
async function buildBasePrograms(
  items: ProgramListItem[],
): Promise<{ base: Program[]; missing: ProgramListItem[] }> {
  const base: Program[] = [];
  const missing: ProgramListItem[] = [];

  for (const p of items) {
    let cached: CachedMeta | null = null;
    if (p.guid && p.version) {
      cached = await getCachedMeta(p.guid, p.version);
    }
    if (cached) {
      base.push(toProgram(p, cached));
    } else {
      base.push({
        id: p.id, guid: p.guid, name: p.name || `Program ${p.id}`,
        desc: '', author: defaultMeta.author, size: '', version: p.version,
        cover: defaultMeta.cover, pulse: defaultMeta.pulse, category: defaultMeta.category,
        params: [],
      });
      missing.push(p);
    }
  }

  return { base, missing };
}

const defaultMeta: CachedMeta = {
  name: '', desc: '', author: 'built-in', category: 'Effects',
  cover: { from: '#555555', to: '#999999', angle: 135 }, pulse: '#888888',
};

async function resolveProgramsMeta(items: ProgramListItem[]): Promise<Program[]> {
  return Promise.all(
    items.map(async (p) => {
      let meta: CachedMeta = { ...defaultMeta, name: p.name || `Program ${p.id}` };

      if (p.guid && p.version) {
        const cached = await getCachedMeta(p.guid, p.version);
        if (cached) {
          meta = cached;
          return {
            id: p.id, guid: p.guid, name: meta.name, desc: meta.desc, author: meta.author,
            size: '', version: p.version, cover: meta.cover, coverSvg: meta.coverSvg,
            pulse: meta.pulse, category: meta.category, params: [], slug: meta.slug,
            i18n: meta.i18n,
          };
        }
      }

      let metaVersion: string | undefined;
      try {
        const raw = await getMeta(p.id);
        if (raw && raw.name) {
          metaVersion = raw.version;
          meta = {
            name: raw.name,
            desc: raw.desc || '',
            author: raw.author || 'built-in',
            category: raw.category || 'Effects',
            cover: raw.cover || defaultMeta.cover,
            coverSvg: raw.coverSvg,
            pulse: raw.pulse || '#888888',
            tags: raw.tags,
            slug: raw.slug,
            i18n: raw.i18n,
          };
          if (p.guid && p.version) {
            await setCachedMeta(p.guid, p.version, meta);
          }
        }
      } catch {
        // Use defaults
      }

      return {
        id: p.id, guid: p.guid, name: meta.name, desc: meta.desc, author: meta.author,
        size: '', version: p.version || metaVersion, cover: meta.cover, coverSvg: meta.coverSvg,
        pulse: meta.pulse, category: meta.category, params: [], slug: meta.slug,
        i18n: meta.i18n,
      };
    }),
  );
}

export async function refreshPrograms(): Promise<void> {
  const { setPrograms, setActiveId } = useProgramStore.getState();
  await invalidateCache();
  const [programList, activeId] = await Promise.all([getPrograms(), getActiveProgram()]);
  const programs = await resolveProgramsMeta(programList as ProgramListItem[]);
  setPrograms(programs);
  setActiveId(activeId);

  const slugs = programs.map((p) => p.slug).filter(Boolean) as string[];
  useMarketStore.getState().setInstalledSlugs(slugs);
}
