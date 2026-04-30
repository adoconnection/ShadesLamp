import React, { useEffect, useState, useRef, useCallback } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useProgramStore } from '../store/useProgramStore';
import { useFavoritesStore } from '../store/useFavoritesStore';
import { useBleStore } from '../store/useBleStore';
import { getParams, getParamValues, setParam, setActiveProgram } from '../ble/commands';
import { Param } from '../types/program';
import NavButton from '../components/NavButton';
import ParamControl from '../components/ParamControl';
import { BackIcon, StarFillIcon, StarOutlineIcon } from '../components/Icon';
import { gradientColors } from '../utils/color';
import { padId } from '../utils/format';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'ProgramDetail'>;

export default function ProgramDetailScreen({ route, navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { programId } = route.params;
  const { programs, activeId, setActiveId, updateParamValue, setProgramParams } = useProgramStore();
  const { favorites, toggleFavorite } = useFavoritesStore();
  const { connectionState } = useBleStore();
  const [loadingParams, setLoadingParams] = useState(false);

  const program = programs.find((p) => p.id === programId);

  // Debounce timer refs
  const debounceTimers = useRef<Map<number, ReturnType<typeof setTimeout>>>(new Map());

  // Load params from BLE if not yet loaded
  useEffect(() => {
    if (!program || program.params.length > 0) return;
    if (connectionState !== 'connected') return;

    setLoadingParams(true);
    Promise.all([getParams(programId), getParamValues(programId)])
      .then(([paramsSchema, paramValues]) => {
        if (!Array.isArray(paramsSchema)) {
          console.warn('[ProgramDetail] paramsSchema is not array:', paramsSchema);
          return;
        }
        const params: Param[] = paramsSchema.map((p: any) => ({
          id: p.id,
          name: p.name || `Param ${p.id}`,
          type: p.type || 'int',
          min: p.min,
          max: p.max,
          default: p.default ?? 0,
          value: paramValues?.[String(p.id)] ?? p.default ?? 0,
          desc: p.desc || '',
          options: p.options,
        }));
        setProgramParams(programId, params);
      })
      .catch((err) => console.warn('[ProgramDetail] Failed to load params:', err))
      .finally(() => setLoadingParams(false));
  }, [programId, program, connectionState, setProgramParams]);

  const handleParamChange = useCallback((paramId: number, value: number, isFloat: boolean) => {
    updateParamValue(programId, paramId, value);

    // Debounced BLE write
    if (connectionState === 'connected') {
      const existing = debounceTimers.current.get(paramId);
      if (existing) clearTimeout(existing);

      const timer = setTimeout(() => {
        setParam(programId, paramId, value, isFloat).catch((err) =>
          console.warn('setParam BLE error:', err),
        );
        debounceTimers.current.delete(paramId);
      }, 200);

      debounceTimers.current.set(paramId, timer);
    }
  }, [programId, connectionState, updateParamValue]);

  const handleActivate = useCallback(async () => {
    if (connectionState === 'connected') {
      try {
        await setActiveProgram(programId);
      } catch (err) {
        console.warn('Failed to activate:', err);
      }
    }
    setActiveId(programId);
  }, [programId, connectionState, setActiveId]);

  if (!program) return null;

  const isActive = activeId === programId;
  const isFavorite = favorites.includes(programId);
  const accent = program.pulse;
  const disabled = connectionState !== 'connected';

  return (
    <ScrollView style={styles.container} bounces={false}>
      {/* Hero header */}
      <View style={styles.heroSection}>
        <LinearGradient
          colors={gradientColors(program.cover)}
          start={{ x: 0, y: 0 }}
          end={{ x: 1, y: 1 }}
          style={StyleSheet.absoluteFill}
        />
        <LinearGradient
          colors={['transparent', colors.bg]}
          locations={[0.6, 1]}
          style={StyleSheet.absoluteFill}
        />

        <View style={[styles.nav, { paddingTop: insets.top + 8 }]}>
          <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
          <NavButton
            icon={isFavorite ? <StarFillIcon color="#0A0A08" /> : <StarOutlineIcon />}
            onPress={() => toggleFavorite(programId)}
            active={isFavorite}
            accent="#FCD34D"
          />
        </View>

        <View style={styles.heroInfo}>
          <Text style={styles.heroLabel}>
            ID {padId(program.id)} · {program.category}
          </Text>
          <Text style={styles.heroTitle}>{program.name}</Text>
          {program.desc ? <Text style={styles.heroDesc}>{program.desc}</Text> : null}
        </View>
      </View>

      {/* Disconnected banner */}
      {disabled && (
        <View style={styles.disconnectedBanner}>
          <Text style={styles.disconnectedText}>Lamp disconnected</Text>
        </View>
      )}

      {/* Activate button */}
      <View style={styles.activateWrap}>
        <Pressable
          onPress={() => !isActive && !disabled && handleActivate()}
          disabled={disabled && !isActive}
          style={[
            styles.activateBtn,
            {
              backgroundColor: isActive ? 'rgba(255,255,255,0.06)' : accent,
              opacity: disabled && !isActive ? 0.4 : 1,
            },
          ]}
        >
          {isActive ? (
            <View style={styles.runningRow}>
              <View style={styles.eqBars}>
                {[0, 1, 2, 3].map((i) => (
                  <View key={i} style={[styles.eqBar, { height: 6, backgroundColor: accent }]} />
                ))}
              </View>
              <Text style={[styles.activateText, { color: accent }]}>Running on Lamp</Text>
            </View>
          ) : (
            <Text style={[styles.activateText, { color: '#0A0A08' }]}>Set Active</Text>
          )}
        </Pressable>
      </View>

      {/* Meta strip */}
      <View style={styles.metaStrip}>
        <MetaCol label="AUTHOR" value={program.author} />
        <MetaCol label="VERSION" value={program.version || '—'} />
      </View>

      {/* Parameters */}
      <Text style={styles.sectionTitle}>Parameters</Text>
      <View style={styles.params}>
        {loadingParams && (
          <Text style={styles.loadingText}>Loading parameters…</Text>
        )}
        {program.params.map((p) => (
          <ParamControl
            key={p.id}
            param={p}
            accent={accent}
            onChange={(v) => handleParamChange(p.id, v, p.type === 'float')}
            disabled={disabled}
          />
        ))}
        {!loadingParams && program.params.length === 0 && (
          <Text style={styles.loadingText}>No parameters</Text>
        )}
      </View>

      {/* File info */}
      <Text style={styles.sectionTitle}>File</Text>
      <View style={styles.fileCard}>
        <View style={styles.fileRow}>
          <View>
            <Text style={styles.fileName}>{program.id}.wasm</Text>
            <Text style={styles.filePath}>
              littlefs:/programs/{program.id}.wasm
            </Text>
          </View>
          {program.author === 'built-in' && (
            <View style={styles.builtInBadge}>
              <Text style={styles.builtInText}>BUILT-IN</Text>
            </View>
          )}
        </View>
      </View>

      <View style={{ height: 40 }} />
    </ScrollView>
  );
}

function MetaCol({ label, value }: { label: string; value: string }) {
  return (
    <View style={metaStyles.col}>
      <Text style={metaStyles.label}>{label}</Text>
      <Text style={metaStyles.value}>{value}</Text>
    </View>
  );
}

const metaStyles = StyleSheet.create({
  col: {
    flex: 1,
  },
  label: {
    fontFamily: fonts.mono,
    fontSize: 10,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.4)',
  },
  value: {
    fontSize: 13,
    color: colors.text,
    marginTop: 2,
  },
});

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: colors.bg,
  },
  heroSection: {
    paddingBottom: 28,
    paddingHorizontal: 20,
  },
  nav: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  heroInfo: {
    marginTop: 50,
  },
  heroLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(255,255,255,0.7)',
    textTransform: 'uppercase',
  },
  heroTitle: {
    fontSize: 36,
    fontWeight: '800',
    color: '#FAFAF7',
    letterSpacing: -1,
    lineHeight: 38,
    marginTop: 6,
  },
  heroDesc: {
    fontSize: 14,
    color: 'rgba(255,255,255,0.85)',
    marginTop: 6,
  },
  activateWrap: {
    paddingHorizontal: 20,
    marginTop: -8,
  },
  activateBtn: {
    borderRadius: 18,
    padding: 15,
    alignItems: 'center',
    justifyContent: 'center',
  },
  activateText: {
    fontSize: 15,
    fontWeight: '700',
    letterSpacing: -0.2,
  },
  runningRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  eqBars: {
    flexDirection: 'row',
    gap: 2.5,
    alignItems: 'flex-end',
    height: 12,
  },
  eqBar: {
    width: 2.5,
    borderRadius: 1,
  },
  metaStrip: {
    paddingHorizontal: 20,
    paddingTop: 20,
    paddingBottom: 6,
    flexDirection: 'row',
    gap: 24,
  },
  sectionTitle: {
    paddingHorizontal: 20,
    paddingTop: 20,
    paddingBottom: 8,
    fontSize: 17,
    fontWeight: '700',
    color: colors.text,
    letterSpacing: -0.3,
  },
  params: {
    paddingHorizontal: 20,
    paddingBottom: 24,
    gap: 14,
  },
  loadingText: {
    fontFamily: fonts.mono,
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  fileCard: {
    marginHorizontal: 20,
    marginBottom: 16,
    backgroundColor: 'rgba(255,255,255,0.04)',
    borderColor: 'rgba(255,255,255,0.06)',
    borderWidth: 0.5,
    borderRadius: 18,
    padding: 14,
    paddingHorizontal: 16,
  },
  fileRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  fileName: {
    fontSize: 14,
    fontWeight: '600',
    color: colors.text,
  },
  filePath: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: 'rgba(250,250,247,0.5)',
    marginTop: 3,
  },
  builtInBadge: {
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 8,
    paddingVertical: 4,
    paddingHorizontal: 8,
  },
  builtInText: {
    fontFamily: fonts.mono,
    fontSize: 10,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.7)',
  },
  disconnectedBanner: {
    marginHorizontal: 20,
    marginBottom: 8,
    paddingVertical: 10,
    paddingHorizontal: 16,
    backgroundColor: 'rgba(248,113,113,0.08)',
    borderColor: 'rgba(248,113,113,0.2)',
    borderWidth: 0.5,
    borderRadius: 12,
    alignItems: 'center',
  },
  disconnectedText: {
    fontFamily: fonts.mono,
    fontSize: 12,
    color: '#F87171',
  },
});
