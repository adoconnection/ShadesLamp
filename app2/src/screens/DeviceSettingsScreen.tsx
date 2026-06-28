import React, { useEffect, useState } from 'react';
import { View, Text, ScrollView, StyleSheet, Alert, Pressable, Modal, TextInput } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { reboot, setPower, getHwConfig, clearStorage, setDeviceName } from '../ble/commands';
import { refreshPrograms } from '../ble/connectFlow';
import NavButton from '../components/NavButton';
import Card from '../components/Card';
import SectionLabel from '../components/SectionLabel';
import SettingsRow from '../components/SettingsRow';
import { BackIcon, EditIcon } from '../components/Icon';
import { t } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'DeviceSettings'>;

export default function DeviceSettingsScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { deviceInfo, connectionState, powerOn, setPowerOn, setDeviceInfo } = useBleStore();
  const { programs } = useProgramStore();
  const [renameOpen, setRenameOpen] = useState(false);
  const [nameInput, setNameInput] = useState('');
  const connected = connectionState === 'connected';

  const openRename = () => { setNameInput(deviceInfo.name); setRenameOpen(true); };

  const saveName = async () => {
    const name = nameInput.trim();
    if (!name || name === deviceInfo.name || !connected) { setRenameOpen(false); return; }
    // The firmware caps the name at 20 bytes (UTF-8), so a Cyrillic name can hit
    // the limit sooner than 20 characters — check before sending.
    if (new TextEncoder().encode(name).length > 20) {
      Alert.alert(t('renameDevice'), t('renameTooLong'));
      return;                              // keep the modal open so the user can shorten it
    }
    setRenameOpen(false);
    const prev = deviceInfo.name;
    setDeviceInfo({ name });               // optimistic
    try {
      await setDeviceName(name);
    } catch (err) {
      setDeviceInfo({ name: prev });       // revert on failure
      Alert.alert(t('renameDevice'), t('renameFailed'));
    }
  };

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

      <Pressable
        style={styles.titleArea}
        onPress={connected ? openRename : undefined}
        disabled={!connected}
        accessibilityRole="button"
        accessibilityLabel={t('renameDevice')}
      >
        <Text style={styles.titleLabel}>{deviceInfo.mac || 'Shades Lamp'}</Text>
        <View style={styles.titleRow}>
          <Text style={styles.title} numberOfLines={1}>{deviceInfo.name}</Text>
          {connected && <EditIcon size={18} color="rgba(250,250,247,0.5)" />}
        </View>
      </Pressable>

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
        {/* These act on the lamp, so they're disabled without a live link. */}
        <SettingsRow label={t('restart')} onPress={handleRestart} disabled={!connected} />
        <SettingsRow label={t('clearMemory')} onPress={handleClearMemory} danger disabled={!connected} />
        <SettingsRow
          label={t('power')}
          toggle
          defaultOn={powerOn}
          disabled={!connected}
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

      <Modal visible={renameOpen} transparent animationType="fade" onRequestClose={() => setRenameOpen(false)}>
        <Pressable style={renameStyles.backdrop} onPress={() => setRenameOpen(false)}>
          <Pressable style={renameStyles.sheet} onPress={() => {}}>
            <Text style={renameStyles.title}>{t('renameDevice')}</Text>
            <TextInput
              style={renameStyles.input}
              value={nameInput}
              onChangeText={setNameInput}
              placeholder={deviceInfo.name}
              placeholderTextColor="rgba(250,250,247,0.4)"
              maxLength={20}
              autoFocus
              returnKeyType="done"
              onSubmitEditing={saveName}
            />
            <Text style={renameStyles.note}>{t('renameRebootNote')}</Text>
            <Pressable style={renameStyles.save} onPress={saveName}>
              <Text style={renameStyles.saveText}>{t('save')}</Text>
            </Pressable>
          </Pressable>
        </Pressable>
      </Modal>
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

const renameStyles = StyleSheet.create({
  backdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.55)', justifyContent: 'center', padding: 28 },
  sheet: { backgroundColor: colors.surface, borderRadius: 20, padding: 18 },
  title: { fontSize: 16, fontWeight: '700', color: colors.text, marginBottom: 12 },
  input: {
    backgroundColor: 'rgba(255,255,255,0.06)', borderRadius: 12, paddingHorizontal: 14, paddingVertical: 12,
    fontSize: 16, color: colors.text,
  },
  note: { marginTop: 12, fontSize: 12, lineHeight: 17, color: 'rgba(250,250,247,0.5)' },
  save: { marginTop: 14, paddingVertical: 13, borderRadius: 14, backgroundColor: colors.accent, alignItems: 'center' },
  saveText: { fontSize: 14, fontWeight: '700', color: colors.accentDark },
});

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 12 },
  titleArea: { paddingHorizontal: 20, paddingTop: 12, paddingBottom: 16 },
  titleLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)', textTransform: 'uppercase' },
  titleRow: { flexDirection: 'row', alignItems: 'center', gap: 10, marginTop: 4 },
  title: { fontSize: 30, fontWeight: '800', color: colors.text, letterSpacing: -0.7, flexShrink: 1 },
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
