import AsyncStorage from '@react-native-async-storage/async-storage';
import { Device } from 'react-native-ble-plx';
import { connectToDevice, setOnDisconnected } from './manager';
import { getPrograms, getActiveProgram, getHwConfig, getDeviceName, getMeta, getStorage } from './commands';
import { Program, ProgramListItem } from '../types/program';
import { getCachedMeta, setCachedMeta, CachedMeta } from '../store/useMetaCache';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';

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
  const { setConnectionState, setDeviceId, setDeviceMac, setDeviceInfo } = useBleStore.getState();
  const { setPrograms, setActiveId } = useProgramStore.getState();

  setConnectionState('connecting');

  const device = await connectToDevice(deviceId);
  setDeviceId(deviceId);
  setDeviceMac(device.id);

  setOnDisconnected(() => {
    setConnectionState('disconnected');
    onDisconnected?.();
  });

  const [programList, activeId, hwConfig, deviceName, storageInfo] = await Promise.all([
    getPrograms(),
    getActiveProgram(),
    getHwConfig(),
    getDeviceName(),
    getStorage().catch(() => ({ used: 0, total: 0, free: 0 })),
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

      if (p.guid && p.version) {
        const cached = await getCachedMeta(p.guid, p.version);
        if (cached) {
          meta = cached;
        } else {
          try {
            const raw = await getMeta(p.id);
            if (raw && raw.name) {
              meta = {
                name: raw.name,
                desc: raw.desc || '',
                author: raw.author || 'built-in',
                category: raw.category || 'Effects',
                cover: raw.cover || defaultMeta.cover,
                pulse: raw.pulse || '#888888',
                tags: raw.tags,
              };
              await setCachedMeta(p.guid, p.version, meta);
            }
          } catch {
            // Use defaults
          }
        }
      }

      return {
        id: p.id,
        name: meta.name,
        desc: meta.desc,
        author: meta.author,
        size: '',
        cover: meta.cover,
        pulse: meta.pulse,
        category: meta.category,
        params: [],
      };
    }),
  );

  setPrograms(programs);
  setActiveId(activeId);

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
