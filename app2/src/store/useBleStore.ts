import { create } from 'zustand';
import { ConnectionState, DeviceInfo } from '../types/ble';

const DEFAULT_DEVICE_INFO: DeviceInfo = {
  name: 'Shades LED Lamp',
  serial: '—',
  mac: '—',
  firmware: '—',
  matrix: '—',
  storage: { used: 0, total: 256 },
  rssi: 0,
};

interface BleState {
  connectionState: ConnectionState;
  deviceId: string | null;
  deviceMac: string;
  deviceInfo: DeviceInfo;
  powerOn: boolean;
  // Background data load after a connection is established. `syncProgress` is
  // non-null while program metadata is still streaming in; `playlistsLoading`
  // is true while the lamp's playlists are being fetched. Both drive the UI
  // spinners so the app never silently looks "fully loaded" mid-sync.
  syncProgress: { done: number; total: number } | null;
  playlistsLoading: boolean;
  setConnectionState: (state: ConnectionState) => void;
  setDeviceId: (id: string | null) => void;
  setDeviceMac: (mac: string) => void;
  setDeviceInfo: (info: Partial<DeviceInfo>) => void;
  setPowerOn: (on: boolean) => void;
  setSyncProgress: (p: { done: number; total: number } | null) => void;
  setPlaylistsLoading: (loading: boolean) => void;
  reset: () => void;
}

export const useBleStore = create<BleState>((set) => ({
  connectionState: 'disconnected',
  deviceId: null,
  deviceMac: '',
  deviceInfo: DEFAULT_DEVICE_INFO,
  powerOn: true,
  syncProgress: null,
  playlistsLoading: false,
  setConnectionState: (connectionState) => set({ connectionState }),
  setDeviceId: (deviceId) => set({ deviceId }),
  setDeviceMac: (deviceMac) => set({ deviceMac }),
  setDeviceInfo: (info) => set((s) => ({ deviceInfo: { ...s.deviceInfo, ...info } })),
  setPowerOn: (powerOn) => set({ powerOn }),
  setSyncProgress: (syncProgress) => set({ syncProgress }),
  setPlaylistsLoading: (playlistsLoading) => set({ playlistsLoading }),
  reset: () => set({ connectionState: 'disconnected', deviceId: null, deviceMac: '', deviceInfo: DEFAULT_DEVICE_INFO, powerOn: true, syncProgress: null, playlistsLoading: false }),
}));
