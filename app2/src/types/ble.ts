export type ConnectionState = 'connected' | 'connecting' | 'disconnected';

export interface BleDevice {
  name: string;
  mac: string;
  rssi: number;
  paired: boolean;
}

export interface DeviceInfo {
  name: string;
  serial: string;
  mac: string;
  firmware: string;
  matrix: string;
  storage: { used: number; total: number };
  uptime: string;
  rssi: number;
}
