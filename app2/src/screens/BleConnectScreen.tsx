import React, { useState, useEffect, useCallback } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  withDelay,
  Easing,
} from 'react-native-reanimated';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { Device } from 'react-native-ble-plx';
import { RootStackParamList } from '../types/navigation';
import { useBleStore } from '../store/useBleStore';
import { scanDevices } from '../ble/manager';
import { connectAndLoadDevice } from '../ble/connectFlow';
import NavButton from '../components/NavButton';
import { BackIcon, RefreshIcon, BluetoothIcon, SignalIcon } from '../components/Icon';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'BleConnect'>;

interface ScannedDevice {
  id: string;
  name: string;
  rssi: number;
}

export default function BleConnectScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { connectionState, setConnectionState } = useBleStore();
  const [scanning, setScanning] = useState(true);
  const [devices, setDevices] = useState<ScannedDevice[]>([]);
  const [connectingId, setConnectingId] = useState<string | null>(null);
  const scanIntervalRef = React.useRef<ReturnType<typeof setInterval> | null>(null);

  const doScan = useCallback(() => {
    setScanning(true);
    setDevices([]);
    const seen = new Map<string, ScannedDevice>();

    scanDevices((device: Device) => {
      if (!device.id) return;
      const entry: ScannedDevice = {
        id: device.id,
        name: device.localName || device.name || 'Unknown',
        rssi: device.rssi ?? -100,
      };
      seen.set(device.id, entry);
      setDevices(Array.from(seen.values()));
    }, 4000).then(() => setScanning(false));
  }, []);

  useEffect(() => {
    doScan();
    scanIntervalRef.current = setInterval(doScan, 10000);
    return () => {
      if (scanIntervalRef.current) clearInterval(scanIntervalRef.current);
    };
  }, [doScan]);

  const handleConnect = async (deviceId: string) => {
    if (scanIntervalRef.current) {
      clearInterval(scanIntervalRef.current);
      scanIntervalRef.current = null;
    }
    setConnectingId(deviceId);

    try {
      await connectAndLoadDevice(deviceId);
      navigation.goBack();
    } catch (err) {
      console.warn('BLE connect error:', err);
      setConnectionState('disconnected');
      setConnectingId(null);
    }
  };

  return (
    <ScrollView style={styles.container}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        <Pressable onPress={doScan} style={styles.rescanBtn}>
          <RefreshIcon color={colors.text} />
          <Text style={styles.rescanText}>Rescan</Text>
        </Pressable>
      </View>

      <View style={styles.titleArea}>
        <Text style={styles.titleLabel}>BLE GATT · 0000ff00</Text>
        <Text style={styles.title}>Devices</Text>
      </View>

      {/* BLE Animation */}
      <View style={styles.animArea}>
        <View style={styles.animCenter}>
          {scanning && [0, 1, 2].map((i) => (
            <ScanRing key={i} delay={i * 800} />
          ))}
          <View style={styles.bleCircle}>
            <BluetoothIcon size={24} color="#60A5FA" />
          </View>
        </View>
        <Text style={styles.scanStatus}>
          {scanning ? 'Scanning…' : `${devices.length} device${devices.length !== 1 ? 's' : ''} found`}
        </Text>
      </View>

      {/* Device list — only shown when devices found */}
      {devices.length > 0 && (
        <View style={styles.listArea}>
          <Text style={styles.listLabel}>NEARBY</Text>
          <View style={styles.listCard}>
            {devices.map((d, i) => (
              <Pressable
                key={d.id}
                onPress={() => handleConnect(d.id)}
                disabled={connectingId !== null}
                style={[styles.deviceRow, i < devices.length - 1 && styles.deviceBorder]}
              >
                <View style={styles.bleIcon}>
                  <BluetoothIcon size={18} color="#60A5FA" />
                </View>
                <View style={styles.deviceInfo}>
                  <View style={styles.deviceNameRow}>
                    <Text style={styles.deviceName}>{d.name}</Text>
                    {connectingId === d.id && (
                      <Text style={styles.connectingLabel}>connecting…</Text>
                    )}
                    {connectionState === 'connected' && connectingId === d.id && (
                      <Text style={styles.connectedLabel}>● connected</Text>
                    )}
                  </View>
                  <Text style={styles.deviceMac}>{d.id.length > 17 ? d.id.slice(-17) : d.id} · {d.rssi} dBm</Text>
                </View>
                <SignalIcon rssi={d.rssi} color="rgba(250,250,247,0.5)" />
              </Pressable>
            ))}
          </View>
        </View>
      )}
    </ScrollView>
  );
}

function ScanRing({ delay }: { delay: number }) {
  const scale = useSharedValue(0.5);
  const opacity = useSharedValue(1);

  useEffect(() => {
    scale.value = withDelay(
      delay,
      withRepeat(
        withTiming(2, { duration: 2400, easing: Easing.out(Easing.ease) }),
        -1,
      ),
    );
    opacity.value = withDelay(
      delay,
      withRepeat(
        withTiming(0, { duration: 2400, easing: Easing.out(Easing.ease) }),
        -1,
      ),
    );
  }, []);

  const animStyle = useAnimatedStyle(() => ({
    transform: [{ scale: scale.value }],
    opacity: opacity.value,
  }));

  return (
    <Animated.View style={[styles.scanRing, animStyle]} />
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 12, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  rescanBtn: { flexDirection: 'row', alignItems: 'center', gap: 6, backgroundColor: 'rgba(255,255,255,0.06)', borderRadius: 999, paddingVertical: 7, paddingHorizontal: 14 },
  rescanText: { fontSize: 12, fontWeight: '500', color: colors.text },
  titleArea: { paddingHorizontal: 20, paddingTop: 12, paddingBottom: 8 },
  titleLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)', textTransform: 'uppercase' },
  title: { fontSize: 30, fontWeight: '800', color: colors.text, letterSpacing: -0.7, marginTop: 4 },
  animArea: { alignItems: 'center', paddingVertical: 20, paddingBottom: 30 },
  animCenter: { width: 120, height: 120, alignItems: 'center', justifyContent: 'center' },
  scanRing: { position: 'absolute', width: 120, height: 120, borderRadius: 60, borderWidth: 1, borderColor: 'rgba(96,165,250,0.4)' },
  bleCircle: { width: 64, height: 64, borderRadius: 32, backgroundColor: 'rgba(96,165,250,0.12)', borderWidth: 1, borderColor: 'rgba(96,165,250,0.3)', alignItems: 'center', justifyContent: 'center' },
  scanStatus: { fontFamily: fonts.mono, fontSize: 13, color: 'rgba(250,250,247,0.6)', marginTop: 8 },
  listArea: { paddingHorizontal: 20, paddingBottom: 40 },
  listLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.4)', marginBottom: 8, paddingLeft: 4 },
  listCard: { backgroundColor: 'rgba(255,255,255,0.04)', borderColor: 'rgba(255,255,255,0.06)', borderWidth: 0.5, borderRadius: 18, overflow: 'hidden' },
  deviceRow: { flexDirection: 'row', alignItems: 'center', gap: 12, padding: 14, paddingHorizontal: 16 },
  deviceBorder: { borderBottomWidth: 0.5, borderBottomColor: 'rgba(255,255,255,0.06)' },
  bleIcon: { width: 36, height: 36, borderRadius: 10, backgroundColor: 'rgba(96,165,250,0.12)', alignItems: 'center', justifyContent: 'center' },
  deviceInfo: { flex: 1, minWidth: 0 },
  deviceNameRow: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  deviceName: { fontSize: 14, fontWeight: '600', color: colors.text },
  connectedLabel: { fontFamily: fonts.mono, fontSize: 11, color: colors.green },
  connectingLabel: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.6)' },
  deviceMac: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 2 },
  emptyText: { fontSize: 13, color: 'rgba(250,250,247,0.5)' },
  uuidLabel: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.4)', marginTop: 12, paddingLeft: 4, lineHeight: 16 },
});
