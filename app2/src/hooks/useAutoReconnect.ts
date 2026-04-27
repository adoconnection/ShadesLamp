import { useEffect, useRef } from 'react';
import { useBleStore } from '../store/useBleStore';
import { connectAndLoadDevice, getLastDevice } from '../ble/connectFlow';

const MAX_RETRIES = 3;
const RETRY_INTERVAL_MS = 5000;

export function useAutoReconnect() {
  const { connectionState } = useBleStore();
  const retryCount = useRef(0);
  const retryTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const lastDeviceId = useRef<string | null>(null);
  const attemptingRef = useRef(false);

  const attemptReconnect = async () => {
    if (attemptingRef.current) return;
    const mac = lastDeviceId.current;
    if (!mac) return;

    attemptingRef.current = true;
    try {
      await connectAndLoadDevice(mac, () => {
        // On disconnect: schedule retries
        retryCount.current = 0;
        scheduleRetry();
      });
      retryCount.current = 0;
    } catch {
      retryCount.current++;
      if (retryCount.current < MAX_RETRIES) {
        scheduleRetry();
      }
    } finally {
      attemptingRef.current = false;
    }
  };

  const scheduleRetry = () => {
    if (retryTimer.current) clearTimeout(retryTimer.current);
    retryTimer.current = setTimeout(attemptReconnect, RETRY_INTERVAL_MS);
  };

  // On mount: try to reconnect to last device
  useEffect(() => {
    getLastDevice().then((mac) => {
      if (mac) {
        lastDeviceId.current = mac;
        attemptReconnect();
      }
    });

    return () => {
      if (retryTimer.current) clearTimeout(retryTimer.current);
    };
  }, []);

  // Track disconnects that happen during the session
  useEffect(() => {
    if (connectionState === 'connected') {
      // Store the current device for future reconnects
      const { deviceId } = useBleStore.getState();
      if (deviceId) lastDeviceId.current = deviceId;
      retryCount.current = 0;
    }
  }, [connectionState]);
}
