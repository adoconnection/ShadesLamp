import AsyncStorage from '@react-native-async-storage/async-storage';
import { Device } from 'react-native-ble-plx';
import { connectToDevice, setOnDisconnected, setOnActiveChanged, setOnEvent } from './manager';
import { getPrograms, getActiveProgram, getHwConfig, getDeviceName, getMeta, getStorage, getPower } from './commands';
import { EVT } from './constants';
import { Program, ProgramListItem } from '../types/program';
import { getCachedMeta, setCachedMeta, CachedMeta } from '../store/useMetaCache';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { useMarketStore } from '../store/useMarketStore';

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
  const { setConnectionState, setDeviceId, setDeviceMac, setDeviceInfo, setPowerOn } = useBleStore.getState();
  const { setPrograms, setActiveId } = useProgramStore.getState();

  setConnectionState('connecting');

  const device = await connectToDevice(deviceId);
  setDeviceId(deviceId);
  setDeviceMac(device.id);

  setOnDisconnected(() => {
    setConnectionState('disconnected');
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

  const [programList, activeId, hwConfig, deviceName, storageInfo, powerOn] = await Promise.all([
    getPrograms(),
    getActiveProgram(),
    getHwConfig(),
    getDeviceName(),
    getStorage().catch(() => ({ used: 0, total: 0, free: 0 })),
    getPower().catch(() => true),
  ]);

  // Resolve meta for each program (cache by guid:version)
  const items = programList as ProgramListItem[];
  const defaultMeta: CachedMeta = {
    name: '', desc: '', author: 'built-in', category: 'Effects',
    cover: { from: '#555555', to: '#999999', angle: 135 }, pulse: '#888888',
  };

  const programs: Program[] = await Promise.all(
    items.map(async (p) => {
      let meta: CachedMeta = { ...defaultMeta, name: p.name || `Program ${p.id}` };

      // Try cache first (by guid:version)
      if (p.guid && p.version) {
        const cached = await getCachedMeta(p.guid, p.version);
        if (cached) {
          meta = cached;
          return {
            id: p.id, name: meta.name, desc: meta.desc, author: meta.author,
            size: '', version: p.version, cover: meta.cover, coverSvg: meta.coverSvg,
            pulse: meta.pulse, category: meta.category, params: [], slug: meta.slug,
          };
        }
      }

      // No cache hit — fetch meta from device
      try {
        const raw = await getMeta(p.id);
        if (raw && raw.name) {
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
          };
          if (p.guid && p.version) {
            await setCachedMeta(p.guid, p.version, meta);
          }
        }
      } catch {
        // Use defaults
      }

      return {
        id: p.id, name: meta.name, desc: meta.desc, author: meta.author,
        size: '', version: p.version, cover: meta.cover, coverSvg: meta.coverSvg,
        pulse: meta.pulse, category: meta.category, params: [], slug: meta.slug,
      };
    }),
  );

  setPrograms(programs);
  setActiveId(activeId);
  setPowerOn(powerOn);

  // Restore marketplace installed state from device programs
  const slugs = programs.map((p) => p.slug).filter(Boolean) as string[];
  if (slugs.length > 0) {
    const marketStore = useMarketStore.getState();
    for (const slug of slugs) {
      marketStore.markInstalled(slug);
    }
  }

  const storageUsedKB = Math.round(storageInfo.used / 1024);
  const storageTotalKB = Math.round(storageInfo.total / 1024);
  setDeviceInfo({
    name: deviceName,
    serial: hwConfig.serial || '—',
    mac: device.id,
    matrix: hwConfig.ok ? `${hwConfig.width} × ${hwConfig.height}` : '—',
    storage: { used: storageUsedKB, total: storageTotalKB },
    rssi: device.rssi ?? 0,
  });

  setConnectionState('connected');

  // Persist for auto-reconnect
  await saveLastDevice(deviceId);

  return device;
}
