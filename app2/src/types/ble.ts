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
  rssi: number;
  temp?: number;   // ESP32-S3 chip temperature, °C (from GET_HW_CONFIG)
}
