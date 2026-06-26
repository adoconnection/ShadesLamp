import { BleManager as PlxBleManager, Device, Subscription } from 'react-native-ble-plx';
import { PermissionsAndroid, Platform } from 'react-native';
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
let disconnectSubscription: Subscription | null = null;
// Characteristic notification subscriptions for the active connection. Tracked
// so we can tear them down on disconnect/reconnect and avoid ghost listeners
// (which would otherwise double-process notifications after a reconnect).
let monitorSubs: Subscription[] = [];

function removeMonitors() {
  for (const sub of monitorSubs) {
    try { sub.remove(); } catch {}
  }
  monitorSubs = [];
}

/**
 * Request the runtime permissions BLE needs on Android (scan/connect on API 31+,
 * fine location below that). No-op on iOS. Returns true if all granted.
 */
export async function requestBlePermissions(): Promise<boolean> {
  if (Platform.OS !== 'android') return true;
  try {
    const api = typeof Platform.Version === 'number' ? Platform.Version : parseInt(String(Platform.Version), 10);
    const perms: string[] =
      api >= 31
        ? [
            PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
            PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
          ]
        : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];
    const result: Record<string, string> = await PermissionsAndroid.requestMultiple(perms as any);
    return perms.every((p) => result[p] === PermissionsAndroid.RESULTS.GRANTED);
  } catch {
    return false;
  }
}
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
  onError?: (error: Error) => void,
): Promise<void> {
  const granted = await requestBlePermissions();
  if (!granted) {
    onError?.(new Error('permission-denied'));
    return;
  }
  const mgr = getBleManager();
  return new Promise((resolve) => {
    const timer = setTimeout(() => {
      mgr.stopDeviceScan();
      resolve();
    }, timeoutMs);

    mgr.startDeviceScan([SERVICE_UUID], null, (error, device) => {
      if (error) {
        clearTimeout(timer);
        mgr.stopDeviceScan();
        onError?.(error);
        resolve();
        return;
      }
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

  // Drop any subscriptions left over from a previous connection so the same
  // notification is never delivered to two listeners after a reconnect.
  removeMonitors();

  // Subscribe to response notifications
  monitorSubs.push(device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.RESPONSE,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      responseAssembler.onNotification(new Uint8Array(data));
    },
  ));

  // Subscribe to param values notifications
  monitorSubs.push(device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.PARAM_VALUES,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      paramValuesAssembler.onNotification(new Uint8Array(data));
    },
  ));

  // Subscribe to active program change notifications
  monitorSubs.push(device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.ACTIVE_PROGRAM,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      if (data.length >= 1) {
        onActiveChangedCallback?.(data[0]);
      }
    },
  ));

  // Subscribe to event notifications (program added/deleted)
  monitorSubs.push(device.monitorCharacteristicForService(
    SERVICE_UUID,
    CHAR.EVENTS,
    (error, char) => {
      if (error || !char?.value) return;
      const data = Buffer.from(char.value, 'base64');
      if (data.length >= 2) {
        onEventCallback?.(data[0], data[1]);
      }
    },
  ));

  // Monitor disconnection
  if (disconnectSubscription) {
    disconnectSubscription.remove();
    disconnectSubscription = null;
  }
  disconnectSubscription = mgr.onDeviceDisconnected(deviceId, () => {
    connectedDevice = null;
    removeMonitors();
    onDisconnectedCallback?.();
  });

  connectedDevice = device;
  return device;
}

export async function disconnect(): Promise<void> {
  removeMonitors();
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
