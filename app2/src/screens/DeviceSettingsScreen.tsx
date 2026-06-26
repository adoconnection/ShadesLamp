import React, { useEffect } from 'react';
import { View, Text, ScrollView, StyleSheet, Alert } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { reboot, setPower, getHwConfig, clearStorage } from '../ble/commands';
import { refreshPrograms } from '../ble/connectFlow';
import NavButton from '../components/NavButton';
import Card from '../components/Card';
import SectionLabel from '../components/SectionLabel';
import SettingsRow from '../components/SettingsRow';
import { BackIcon } from '../components/Icon';
import { t } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'DeviceSettings'>;

export default function DeviceSettingsScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { deviceInfo, connectionState, powerOn, setPowerOn, setDeviceInfo } = useBleStore();
  const { programs } = useProgramStore();

  // Live chip-temperature: poll GET_HW_CONFIG every 5 s while on this screen.
  useEffect(() => {
    if (connectionState !== 'connected') return;
    let alive = true;
    const poll = async () => {
      try {
        const hw = await getHwConfig();
        if (alive && hw && hw.ok) {
          const patch: any = {};
          if (typeof hw.temp === 'number') patch.temp = hw.temp;
          if (hw.build != null) patch.firmware = `build ${hw.build}`;
          if (Object.keys(patch).length) setDeviceInfo(patch);
        }
      } catch {}
    };
    poll();
    const id = setInterval(poll, 5000);
    return () => { alive = false; clearInterval(id); };
  }, [connectionState, setDeviceInfo]);

  const handleRestart = () => {
    Alert.alert(t('restartDeviceTitle'), t('restartDeviceMsg'), [
      { text: t('cancel'), style: 'cancel' },
      {
        text: t('restart'),
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

  const handleClearMemory = () => {
    Alert.alert(t('clearMemoryTitle'), t('clearMemoryMsg'), [
      { text: t('cancel'), style: 'cancel' },
      {
        text: t('clear'),
        style: 'destructive',
        onPress: async () => {
          if (connectionState !== 'connected') return;
          try {
            await clearStorage();
            // Give the lamp a moment to wipe flash, then resync the app lists.
            await new Promise((r) => setTimeout(r, 700));
            await refreshPrograms();
          } catch (err) {
            console.warn('Clear memory error:', err);
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
        <Text style={styles.titleLabel}>{deviceInfo.mac || 'Shades Lamp'}</Text>
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
            {connectionState === 'connected'
              ? `${t('online')} · ${deviceInfo.rssi} dBm${typeof deviceInfo.temp === 'number' ? ` · ${Math.round(deviceInfo.temp)}°C` : ''}`
              : t('offline')}
          </Text>
        </View>
        <View style={styles.statsGrid}>
          <StatBlock label={t('matrix')} value={deviceInfo.matrix} />
          <StatBlock label={t('firmware')} value={deviceInfo.firmware} />
          <StatBlock
            label={t('storage')}
            value={`${deviceInfo.storage.total > 0 ? Math.round((deviceInfo.storage.used / deviceInfo.storage.total) * 100) : 0}%`}
            bar={deviceInfo.storage.total > 0 ? deviceInfo.storage.used / deviceInfo.storage.total : 0}
          />
        </View>
      </View>

      <SectionLabel>{t('programsSection')}</SectionLabel>
      <Card>
        <SettingsRow label={t('installed')} detail={String(programs.length)} last />
      </Card>

      <SectionLabel>{t('deviceSection')}</SectionLabel>
      <Card>
        <SettingsRow label={t('restart')} onPress={handleRestart} />
        <SettingsRow label={t('clearMemory')} onPress={handleClearMemory} danger />
        <SettingsRow
          label={t('power')}
          toggle
          defaultOn={powerOn}
          onToggle={async (on: boolean) => {
            setPowerOn(on);
            if (connectionState === 'connected') {
              try { await setPower(on); } catch (e) { setPowerOn(!on); }
            }
          }}
          last
        />
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
  block: { flex: 1, minWidth: 0 },
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
