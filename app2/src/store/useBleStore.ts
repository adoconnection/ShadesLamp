import { create } from 'zustand';
import { ConnectionState, DeviceInfo } from '../types/ble';

const DEFAULT_DEVICE_INFO: DeviceInfo = {
  name: 'Shades LED Lamp',
  serial: '—',
  mac: '—',
  firmware: '—',
  matrix: '—',
  storage: { used: 0, total: 256 },
  uptime: '—',
  rssi: 0,
};

interface BleState {
  connectionState: ConnectionState;
  deviceId: string | null;
  deviceMac: string;
  deviceInfo: DeviceInfo;
  setConnectionState: (state: ConnectionState) => void;
  setDeviceId: (id: string | null) => void;
  setDeviceMac: (mac: string) => void;
  setDeviceInfo: (info: Partial<DeviceInfo>) => void;
  reset: () => void;
}

export const useBleStore = create<BleState>((set) => ({
  connectionState: 'disconnected',
  deviceId: null,
  deviceMac: '',
  deviceInfo: DEFAULT_DEVICE_INFO,
  setConnectionState: (connectionState) => set({ connectionState }),
  setDeviceId: (deviceId) => set({ deviceId }),
  setDeviceMac: (deviceMac) => set({ deviceMac }),
  setDeviceInfo: (info) => set((s) => ({ deviceInfo: { ...s.deviceInfo, ...info } })),
  reset: () => set({ connectionState: 'disconnected', deviceId: null, deviceMac: '', deviceInfo: DEFAULT_DEVICE_INFO }),
}));
