import { BleManager as PlxBleManager, Device, Characteristic } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import { SERVICE_UUID, CHAR, CONNECT_TIMEOUT_MS } from './constants';
import { ChunkedResponseAssembler } from './protocol';

let manager: PlxBleManager | null = null;

export function getBleManager(): PlxBleManager {
  if (!manager) {
    manager = new PlxBleManager();
  }
  return manager;
}

let connectedDevice: Device | null = null;
let disconnectSubscription: { remove: () => void } | null = null;
let onDisconnectedCallback: (() => void) | null = null;
let onActiveChangedCallback: ((id: number) => void) | null = null;
let onEventCallback: ((eventType: number, programId: number) => void) | null = null;
const responseAssembler = new ChunkedResponseAssembler();
const paramValuesAssembler = new ChunkedResponseAssembler();

export function getConnectedDevice(): Device | null {
  return connectedDevice;
}

export async function scanDevices(
  onFound: (device: Device) => void,
  timeoutMs = 5000,
): Promise<void> {
  const mgr = getBleManager();
  return new Promise((resolve) => {
    const timer = setTimeout(() => {
      mgr.stopDeviceScan();
      resolve();
    }, timeoutMs);

    mgr.startDeviceScan([SERVICE_UUID], null, (error, device) => {
      if (error) return;
      if (device) onFound(device);
    });
  });
}

export function setOnDisconnected(cb: (() => void) | null) {
  onDisconnectedCallback = cb;
}

export function setOnActiveChanged(cb: ((id: number) => void) | null) {
  onActiveChangedCallback = cb;
}

export function setOnEvent(cb: ((eventType: number, programId: number) => void) | null) {
  onEventCallback = cb;
}

export async function connectToDevice(deviceId: string): Promise<Device> {
  const mgr = getBleManager();

  const device = await mgr.connectToDevice(deviceId, {
    timeout: CONNECT_TIMEOUT_MS,
    requestMTU: 512,
  });

  await device.discoverAllServicesAndCharacteristics();

  // Subscribe to response notifications
  device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.RESPONSE,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      responseAssembler.onNotification(new Uint8Array(data));
    },
  );

  // Subscribe to param values notifications
  device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.PARAM_VALUES,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      paramValuesAssembler.onNotification(new Uint8Array(data));
    },
  );

  // Subscribe to active program change notifications
  device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.ACTIVE_PROGRAM,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      if (data.length >= 1) {
        onActiveChangedCallback?.(data[0]);
      }
    },
  );

  // Subscribe to event notifications (program added/deleted)
  device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.EVENTS,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      if (data.length >= 2) {
        onEventCallback?.(data[0], data[1]);
      }
    },
  );

  // Monitor disconnection
  disconnectSubscription = mgr.onDeviceDisconnected(deviceId, () => {
    connectedDevice = null;
    onDisconnectedCallback?.();
  });

  connectedDevice = device;
  return device;
}

export async function disconnect(): Promise<void> {
  if (disconnectSubscription) {
    disconnectSubscription.remove();
    disconnectSubscription = null;
  }
  if (connectedDevice) {
    await connectedDevice.cancelConnection();
    connectedDevice = null;
  }
}

export async function writeCommand(bytes: Uint8Array): Promise<any> {
  if (!connectedDevice) throw new Error('Not connected');

  const responsePromise = responseAssembler.waitForResponse();

  await connectedDevice.writeCharacteristicWithResponseForService(
    SERVICE_UUID,
    CHAR.COMMAND,
    Buffer.from(bytes).toString('base64'),
  );

  return responsePromise;
}

export async function writeUploadChunk(bytes: Uint8Array): Promise<void> {
  if (!connectedDevice) throw new Error('Not connected');

  await connectedDevice.writeCharacteristicWithoutResponseForService(
    SERVICE_UUID,
    CHAR.UPLOAD,
    Buffer.from(bytes).toString('base64'),
  );
}

export async function writeActiveProgram(programId: number): Promise<void> {
  if (!connectedDevice) throw new Error('Not connected');

  await connectedDevice.writeCharacteristicWithResponseForService(
    SERVICE_UUID,
    CHAR.ACTIVE_PROGRAM,
    Buffer.from([programId]).toString('base64'),
  );
}

export async function readActiveProgram(): Promise<number> {
  if (!connectedDevice) throw new Error('Not connected');

  const char = await connectedDevice.readCharacteristicForService(
    SERVICE_UUID,
    CHAR.ACTIVE_PROGRAM,
  );

  if (!char.value) return 0;
  const data = Buffer.from(char.value, 'base64');
  return data[0];
}

export { responseAssembler, paramValuesAssembler };
