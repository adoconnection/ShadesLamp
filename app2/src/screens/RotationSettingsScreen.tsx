import React from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useFavoritesStore } from '../store/useFavoritesStore';
import { RotationMode } from '../types/favorites';
import NavButton from '../components/NavButton';
import { BackIcon } from '../components/Icon';
import { t } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'RotationSettings'>;

const MODES: { key: RotationMode; label: () => string }[] = [
  { key: 'off', label: () => t('rotationOff') },
  { key: 'next', label: () => t('rotationNext') },
  { key: 'random', label: () => t('rotationRandom') },
];

// interval presets in seconds
const INTERVALS = [5, 10, 30, 60, 120, 300];

function intervalLabel(sec: number): string {
  return sec >= 60 && sec % 60 === 0 ? t('minShort', { n: sec / 60 }) : t('secShort', { n: sec });
}

export default function RotationSettingsScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const rotationMode = useFavoritesStore((s) => s.rotationMode);
  const rotationIntervalSec = useFavoritesStore((s) => s.rotationIntervalSec);
  const setRotationMode = useFavoritesStore((s) => s.setRotationMode);
  const setRotationIntervalSec = useFavoritesStore((s) => s.setRotationIntervalSec);

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 60 }}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        <Text style={styles.headerTitle}>{t('rotation')}</Text>
        <View style={{ width: 40 }} />
      </View>

      <Text style={styles.sectionLabel}>{t('rotationModeLabel')}</Text>
      <View style={styles.segment}>
        {MODES.map((m) => {
          const on = rotationMode === m.key;
          return (
            <Pressable
              key={m.key}
              onPress={() => setRotationMode(m.key)}
              style={[styles.segItem, on && styles.segItemOn]}
            >
              <Text style={[styles.segText, on && styles.segTextOn]}>{m.label()}</Text>
            </Pressable>
          );
        })}
      </View>

      {rotationMode !== 'off' && (
        <>
          <Text style={styles.sectionLabel}>{t('rotationIntervalLabel')}</Text>
          <View style={styles.intervalGrid}>
            {INTERVALS.map((sec) => {
              const on = rotationIntervalSec === sec;
              return (
                <Pressable
                  key={sec}
                  onPress={() => setRotationIntervalSec(sec)}
                  style={[styles.chip, on && styles.chipOn]}
                >
                  <Text style={[styles.chipText, on && styles.chipTextOn]}>{intervalLabel(sec)}</Text>
                </Pressable>
              );
            })}
          </View>
          <Text style={styles.note}>
            {t('rotationOnDesc', { t: intervalLabel(rotationIntervalSec) })}
          </Text>
        </>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: {
    paddingHorizontal: 20, paddingBottom: 12,
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
  },
  headerTitle: { fontSize: 17, fontWeight: '700', color: colors.text, letterSpacing: -0.3 },
  sectionLabel: {
    fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)',
    paddingHorizontal: 20, paddingTop: 20, paddingBottom: 10, textTransform: 'uppercase',
  },
  segment: {
    marginHorizontal: 20, flexDirection: 'row', gap: 8,
  },
  segItem: {
    flex: 1, paddingVertical: 12, borderRadius: 14, alignItems: 'center',
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
  segItemOn: { backgroundColor: colors.text },
  segText: { fontSize: 14, fontWeight: '600', color: colors.text },
  segTextOn: { color: '#0A0A08' },
  intervalGrid: {
    marginHorizontal: 20, flexDirection: 'row', flexWrap: 'wrap', gap: 8,
  },
  chip: {
    paddingVertical: 10, paddingHorizontal: 16, borderRadius: 999,
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
  chipOn: { backgroundColor: colors.text },
  chipText: { fontSize: 13, fontWeight: '600', color: colors.text },
  chipTextOn: { color: '#0A0A08' },
  note: {
    fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)',
    paddingHorizontal: 20, paddingTop: 16,
  },
});
