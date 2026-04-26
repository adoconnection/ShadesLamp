import React from 'react';
import { View, Text, ScrollView, StyleSheet, Alert } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { reboot } from '../ble/commands';
import NavButton from '../components/NavButton';
import Card from '../components/Card';
import SectionLabel from '../components/SectionLabel';
import SettingsRow from '../components/SettingsRow';
import { BackIcon } from '../components/Icon';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'DeviceSettings'>;

export default function DeviceSettingsScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { deviceInfo, connectionState } = useBleStore();
  const { programs } = useProgramStore();

  const handleRestart = () => {
    Alert.alert('Restart device', 'Are you sure you want to restart the lamp?', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Restart',
        onPress: async () => {
          if (connectionState === 'connected') {
            try {
              await reboot();
            } catch (err) {
              console.warn('Reboot error:', err);
            }
          }
        },
      },
    ]);
  };

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 40 }}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
      </View>

      <View style={styles.titleArea}>
        <Text style={styles.titleLabel}>ESP32-S3 · WasmLED</Text>
        <Text style={styles.title}>{deviceInfo.name}</Text>
      </View>

      {/* Stats hero */}
      <View style={styles.statsCard}>
        <LinearGradient
          colors={['#1F1D1A', '#14130F']}
          start={{ x: 0, y: 0 }}
          end={{ x: 1, y: 1 }}
          style={StyleSheet.absoluteFill}
        />
        <View style={styles.onlineRow}>
          <View style={[styles.onlineDot, connectionState !== 'connected' && { backgroundColor: '#6B7280' }]} />
          <Text style={[styles.onlineText, connectionState !== 'connected' && { color: '#6B7280' }]}>
            {connectionState === 'connected' ? `ONLINE · ${deviceInfo.rssi} dBm` : 'OFFLINE'}
          </Text>
        </View>
        <View style={styles.statsGrid}>
          <StatBlock label="MATRIX" value={deviceInfo.matrix} />
          <StatBlock label="UPTIME" value={deviceInfo.uptime} />
          <StatBlock label="FIRMWARE" value={deviceInfo.firmware} />
          <StatBlock
            label="STORAGE"
            value={`${deviceInfo.storage.used} / ${deviceInfo.storage.total} KB`}
            bar={deviceInfo.storage.total > 0 ? deviceInfo.storage.used / deviceInfo.storage.total : 0}
          />
        </View>
      </View>

      <SectionLabel>CONNECTION</SectionLabel>
      <Card>
        <SettingsRow
          label="BLE Device"
          detail={deviceInfo.name}
          chev
          onPress={() => navigation.navigate('BleConnect')}
        />
        <SettingsRow label="MAC Address" detail={deviceInfo.mac} mono />
        <SettingsRow label="Service UUID" detail="…ff00" mono last />
      </Card>

      <SectionLabel>PROGRAMS</SectionLabel>
      <Card>
        <SettingsRow label="Installed" detail={`${programs.length} of 128`} chev />
        <SettingsRow label="Auto-start last program" toggle defaultOn />
        <SettingsRow label="Marketplace registry" detail="github" chev last />
      </Card>

      <SectionLabel>DEVICE</SectionLabel>
      <Card>
        <SettingsRow label="Restart" onPress={handleRestart} />
        <SettingsRow label="Wipe storage" danger />
        <SettingsRow label="Firmware OTA update" detail="up to date" chev last />
      </Card>

      <View style={styles.footer}>
        <Text style={styles.footerText}>
          Serial {deviceInfo.serial}{'\n'}
          Built with wasm3 · LittleFS · NeoPixelBus
        </Text>
      </View>
    </ScrollView>
  );
}

function StatBlock({ label, value, bar }: { label: string; value: string; bar?: number }) {
  return (
    <View style={statStyles.block}>
      <Text style={statStyles.label}>{label}</Text>
      <Text style={statStyles.value}>{value}</Text>
      {bar !== undefined && (
        <View style={statStyles.barTrack}>
          <View style={[statStyles.barFill, { width: `${bar * 100}%` }]} />
        </View>
      )}
    </View>
  );
}

const statStyles = StyleSheet.create({
  block: {},
  label: { fontFamily: fonts.mono, fontSize: 10, letterSpacing: 1, color: 'rgba(250,250,247,0.4)' },
  value: { fontSize: 17, fontWeight: '700', color: colors.text, marginTop: 4, letterSpacing: -0.3 },
  barTrack: { marginTop: 6, height: 3, backgroundColor: 'rgba(255,255,255,0.08)', borderRadius: 2, overflow: 'hidden' },
  barFill: { height: '100%', backgroundColor: colors.blue },
});

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 12 },
  titleArea: { paddingHorizontal: 20, paddingTop: 12, paddingBottom: 16 },
  titleLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)', textTransform: 'uppercase' },
  title: { fontSize: 30, fontWeight: '800', color: colors.text, letterSpacing: -0.7, marginTop: 4 },
  statsCard: {
    marginHorizontal: 20,
    marginBottom: 20,
    borderRadius: 22,
    padding: 18,
    borderWidth: 0.5,
    borderColor: 'rgba(255,255,255,0.06)',
    overflow: 'hidden',
  },
  onlineRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  onlineDot: { width: 8, height: 8, borderRadius: 4, backgroundColor: colors.green },
  onlineText: { fontFamily: fonts.mono, fontSize: 12, color: colors.green, letterSpacing: 0.5 },
  statsGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 14,
    marginTop: 16,
  },
  footer: { paddingHorizontal: 24, paddingTop: 20 },
  footerText: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.35)', lineHeight: 18 },
});
