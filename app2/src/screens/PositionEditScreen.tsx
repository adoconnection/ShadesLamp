import React, { useCallback, useEffect, useRef, useState } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useProgramStore } from '../store/useProgramStore';
import { usePlaylistStore } from '../store/usePlaylistStore';
import { useBleStore } from '../store/useBleStore';
import { getParams } from '../ble/commands';
import { findProgramForPosition } from '../ble/playlists';
import { Param, Gradient } from '../types/program';
import { PlaylistPosition } from '../types/playlist';
import Cover from '../components/Cover';
import NavButton from '../components/NavButton';
import ParamControl from '../components/ParamControl';
import { BackIcon, PlayIcon } from '../components/Icon';
import { t, localized, localizedParam, localizedOptions } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'PositionEdit'>;

const DEFAULT_COVER: Gradient = { from: '#555555', to: '#999999', angle: 135 };

export default function PositionEditScreen({ route, navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { playlistId, index } = route.params;
  const { programs } = useProgramStore();
  const pl = usePlaylistStore((s) => s.playlists.find((p) => p.id === playlistId));
  const updatePositionParams = usePlaylistStore((s) => s.updatePositionParams);
  const playPosition = usePlaylistStore((s) => s.playPosition);
  const { connectionState } = useBleStore();
  const disabled = connectionState !== 'connected';

  const pos = pl?.positions[index];
  const program = pos ? findProgramForPosition(pos, programs) : undefined;
  const missing = !!pos && !program;

  const accent = program?.pulse || '#888888';
  const cover = program?.cover || DEFAULT_COVER;
  const name = program ? localized(program, 'name', program.name) : pos?.name || `Program ${pos?.prog ?? ''}`;

  // Merged Param[] = program schema (names/ranges/types) + this position's saved
  // values. Built once; store updates from our own writes must not rebuild it.
  const [params, setParams] = useState<Param[] | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(false);
  const builtRef = useRef(false);

  const buildOnce = useCallback((schema: Param[], p: PlaylistPosition): Param[] => {
    const saved = new Map(p.params.map((sp) => [sp.id, sp.value]));
    return schema.map((s) => ({ ...s, value: saved.has(s.id) ? (saved.get(s.id) as number) : s.value }));
  }, []);

  const loadSchema = useCallback(() => {
    if (builtRef.current || !program || !pos) return;

    // Schema already resolved (e.g. the program was opened earlier) — reuse it.
    if (program.params.length > 0) {
      builtRef.current = true;
      setParams(buildOnce(program.params, pos));
      return;
    }
    if (disabled) return;

    setLoading(true);
    setError(false);
    getParams(program.id)
      .then((schemaRaw) => {
        if (!Array.isArray(schemaRaw)) { setError(true); return; }
        const schema: Param[] = schemaRaw.map((p: any) => ({
          id: p.id,
          name: localizedParam(program.i18n, p.id, 'name', p.name || `Param ${p.id}`),
          type: p.type || 'int',
          min: p.min,
          max: p.max,
          default: p.default ?? 0,
          value: p.default ?? 0,
          desc: localizedParam(program.i18n, p.id, 'desc', p.desc || ''),
          options: localizedOptions(program.i18n, p.id, p.options),
        }));
        builtRef.current = true;
        setParams(buildOnce(schema, pos));
      })
      .catch(() => setError(true))
      .finally(() => setLoading(false));
  }, [program, pos, disabled, buildOnce]);

  useEffect(() => { loadSchema(); }, [loadSchema]);

  // Debounced persistence to the lamp (and the store). Each param tick updates
  // local state immediately; the BLE write is coalesced.
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingRef = useRef<Param[] | null>(null);

  const persist = useCallback((next: Param[]) => {
    const snap = next.map((p) => ({ id: p.id, value: p.value, f: p.type === 'float' }));
    updatePositionParams(playlistId, index, snap);
  }, [playlistId, index, updatePositionParams]);

  const flush = useCallback(() => {
    if (debounceRef.current) { clearTimeout(debounceRef.current); debounceRef.current = null; }
    if (pendingRef.current) { persist(pendingRef.current); pendingRef.current = null; }
  }, [persist]);

  // Flush any pending edit when leaving the screen.
  useEffect(() => () => flush(), [flush]);

  const handleChange = useCallback((paramId: number, value: number) => {
    setParams((prev) => {
      if (!prev) return prev;
      const next = prev.map((p) => (p.id === paramId ? { ...p, value } : p));
      pendingRef.current = next;
      if (debounceRef.current) clearTimeout(debounceRef.current);
      debounceRef.current = setTimeout(() => {
        persist(next);
        pendingRef.current = null;
        debounceRef.current = null;
      }, 250);
      return next;
    });
  }, [persist]);

  const onPlay = useCallback(() => {
    flush();                          // make sure edits land before we apply them
    playPosition(playlistId, index);
  }, [flush, playPosition, playlistId, index]);

  if (!pl || !pos) {
    return (
      <View style={styles.container}>
        <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
          <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        </View>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
      </View>

      <ScrollView contentContainerStyle={{ paddingBottom: 80 }}>
        <View style={styles.titleArea}>
          <Cover cover={cover} pulse={accent} size={56} radius={14} />
          <View style={styles.titleInfo}>
            <Text style={styles.title} numberOfLines={2}>{name}</Text>
            <Text style={[styles.subtitle, missing && styles.subtitleMissing]}>
              {missing ? t('programMissing') : t('positionParams')}
            </Text>
          </View>
        </View>

        {missing && <Text style={styles.missingHint}>{t('programMissingDesc')}</Text>}

        <Pressable
          style={[styles.playBtn, (disabled || missing) && { opacity: 0.4 }]}
          onPress={onPlay}
          disabled={disabled || missing}
        >
          <PlayIcon size={16} color="#0A0A08" />
          <Text style={styles.playText}>{t('playThisPosition')}</Text>
        </Pressable>

        <Text style={styles.sectionLabel}>{t('parameters').toUpperCase()}</Text>
        <View style={styles.params}>
          {params && params.length > 0 ? (
            params.map((p) => (
              <ParamControl
                key={p.id}
                param={p}
                accent={accent}
                onChange={(v) => handleChange(p.id, v)}
                disabled={disabled}
              />
            ))
          ) : loading ? (
            <Text style={styles.note}>{t('loadingParameters')}</Text>
          ) : error ? (
            <View style={styles.errorBox}>
              <Text style={styles.errorText}>{t('paramsError')}</Text>
              <Pressable onPress={loadSchema} style={styles.retry}>
                <Text style={styles.retryText}>{t('retry')}</Text>
              </Pressable>
            </View>
          ) : disabled ? (
            <Text style={styles.note}>{t('connectToLoadParams')}</Text>
          ) : (
            <Text style={styles.note}>{t('noParameters')}</Text>
          )}
        </View>

        <Text style={styles.hint}>{t('editPositionHint')}</Text>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 4 },
  titleArea: { flexDirection: 'row', alignItems: 'center', gap: 14, paddingHorizontal: 20, paddingTop: 8, paddingBottom: 16 },
  titleInfo: { flex: 1, minWidth: 0 },
  title: { fontSize: 24, fontWeight: '800', color: colors.text, letterSpacing: -0.5 },
  subtitle: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 4 },
  subtitleMissing: { color: '#F59E0B' },
  missingHint: {
    fontSize: 13, color: 'rgba(250,250,247,0.6)', lineHeight: 19,
    paddingHorizontal: 20, paddingBottom: 12,
  },
  playBtn: {
    marginHorizontal: 20, marginBottom: 8, paddingVertical: 13, borderRadius: 14,
    backgroundColor: '#FAFAF7', flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8,
  },
  playText: { fontSize: 14, fontWeight: '700', color: '#0A0A08' },
  sectionLabel: {
    fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)',
    paddingHorizontal: 20, paddingTop: 18, paddingBottom: 10, textTransform: 'uppercase',
  },
  params: { paddingHorizontal: 20, gap: 14 },
  note: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)' },
  errorBox: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 12 },
  errorText: { fontFamily: fonts.mono, fontSize: 12, color: '#F87171', flex: 1 },
  retry: { paddingVertical: 6, paddingHorizontal: 14, borderRadius: 10, backgroundColor: 'rgba(255,255,255,0.08)' },
  retryText: { fontSize: 12, fontWeight: '600', color: colors.text },
  hint: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.35)', textAlign: 'center', marginTop: 22, paddingHorizontal: 24 },
});
