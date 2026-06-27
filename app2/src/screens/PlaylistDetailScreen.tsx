import React, { useCallback, useState } from 'react';
import { View, Text, Pressable, StyleSheet, Modal, TextInput } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import DraggableFlatList, { RenderItemParams, ScaleDecorator } from 'react-native-draggable-flatlist';
import { Swipeable } from 'react-native-gesture-handler';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { PlaylistPosition, RotationMode } from '../types/playlist';
import { useProgramStore } from '../store/useProgramStore';
import { usePlaylistStore } from '../store/usePlaylistStore';
import { findProgramForPosition } from '../ble/playlists';
import Cover from '../components/Cover';
import NavButton from '../components/NavButton';
import { BackIcon, TrashIcon, PlayIcon, PauseIcon, EditIcon } from '../components/Icon';
import { t, localized } from '../i18n';
import { Gradient } from '../types/program';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'PlaylistDetail'>;

const MODES: { key: RotationMode; label: () => string }[] = [
  { key: 'off', label: () => t('rotationOff') },
  { key: 'next', label: () => t('rotationNext') },
  { key: 'random', label: () => t('rotationRandom') },
];
const INTERVALS = [5, 10, 30, 60, 120, 300];
const intervalLabel = (s: number) => (s >= 60 && s % 60 === 0 ? t('minShort', { n: s / 60 }) : t('secShort', { n: s }));
const DEFAULT_COVER: Gradient = { from: '#555555', to: '#999999', angle: 135 };

export default function PlaylistDetailScreen({ route, navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { playlistId } = route.params;
  const { programs } = useProgramStore();
  const pl = usePlaylistStore((s) => s.playlists.find((p) => p.id === playlistId));
  const playingId = usePlaylistStore((s) => s.playingId);
  const currentIndex = usePlaylistStore((s) => s.currentIndex);
  const { reorder, removePosition, setRotation, play, playPosition, stop, renamePlaylist } = usePlaylistStore();
  const [renameOpen, setRenameOpen] = useState(false);
  const [nameInput, setNameInput] = useState('');

  const display = useCallback((pos: PlaylistPosition) => {
    const prog = findProgramForPosition(pos, programs);
    return {
      missing: !prog,
      name: prog ? localized(prog, 'name', prog.name) : (pos.name || `Program ${pos.prog}`),
      cover: prog?.cover || DEFAULT_COVER,
      pulse: prog?.pulse || '#888888',
    };
  }, [programs]);

  if (!pl) return null;
  const isPlaying = playingId === pl.id;

  // Positions in play order (array order).
  const data = pl.positions;

  const renderItem = ({ item, drag, isActive, getIndex }: RenderItemParams<PlaylistPosition>) => {
    const d = display(item);
    const index = getIndex() ?? 0;
    const isCurrent = isPlaying && currentIndex === index;
    return (
      <ScaleDecorator>
        <Swipeable
          renderRightActions={() => (
            <View style={styles.deleteAction}><TrashIcon size={18} color="#FAFAF7" /></View>
          )}
          rightThreshold={40}
          onSwipeableOpen={() => removePosition(pl.id, index)}
        >
          <Pressable style={styles.row} onPress={() => { if (!d.missing) playPosition(pl.id, index); }} onLongPress={drag} delayLongPress={180} disabled={isActive}>
            <View style={d.missing && styles.coverMissing}>
              <Cover cover={d.cover} pulse={d.pulse} size={44} radius={10} animated={isCurrent && !d.missing} />
            </View>
            <View style={styles.rowInfo}>
              <Text style={[styles.rowName, d.missing && styles.rowNameMissing, isCurrent && !d.missing && { color: d.pulse }]} numberOfLines={1}>{d.name}</Text>
              <Text style={[styles.rowMeta, d.missing && styles.rowMetaMissing]} numberOfLines={1}>
                {d.missing ? t('programMissing') : `${item.params.length} · ${t('parameters').toLowerCase()}`}
              </Text>
            </View>
            <Pressable
              hitSlop={12}
              onPress={() => navigation.navigate('PositionEdit', { playlistId: pl.id, index })}
              style={styles.editBtn}
            >
              <EditIcon size={18} color="rgba(250,250,247,0.55)" />
            </Pressable>
          </Pressable>
        </Swipeable>
      </ScaleDecorator>
    );
  };

  const ListHeader = (
    <>
      <View style={styles.titleArea}>
        <Pressable onPress={() => { setNameInput(pl.name); setRenameOpen(true); }}>
          <Text style={styles.title}>{pl.name}</Text>
        </Pressable>
        <Text style={styles.subtitle}>{t('positionsCount', { n: pl.positions.length })}</Text>
      </View>

      <Pressable
        style={[styles.playBig, isPlaying && { backgroundColor: '#FCD34D' }]}
        onPress={() => (isPlaying ? stop() : play(pl.id))}
        disabled={pl.positions.length === 0}
      >
        {isPlaying ? <PauseIcon size={18} color="#0A0A08" /> : <PlayIcon size={18} color="#0A0A08" />}
        <Text style={styles.playBigText}>{isPlaying ? t('stop') : t('play')}</Text>
      </Pressable>

      <Text style={styles.sectionLabel}>{t('rotationModeLabel')}</Text>
      <View style={styles.segment}>
        {MODES.map((m) => {
          const on = pl.mode === m.key;
          return (
            <Pressable key={m.key} onPress={() => setRotation(pl.id, m.key, pl.interval)} style={[styles.segItem, on && styles.segItemOn]}>
              <Text style={[styles.segText, on && styles.segTextOn]}>{m.label()}</Text>
            </Pressable>
          );
        })}
      </View>

      {pl.mode !== 'off' && (
        <>
          <Text style={styles.sectionLabel}>{t('rotationIntervalLabel')}</Text>
          <View style={styles.intervalGrid}>
            {INTERVALS.map((sec) => {
              const on = pl.interval === sec;
              return (
                <Pressable key={sec} onPress={() => setRotation(pl.id, pl.mode, sec)} style={[styles.chip, on && styles.chipOn]}>
                  <Text style={[styles.chipText, on && styles.chipTextOn]}>{intervalLabel(sec)}</Text>
                </Pressable>
              );
            })}
          </View>
        </>
      )}

      <Text style={styles.sectionLabel}>{t('playlists').toUpperCase()}</Text>
    </>
  );

  return (
    <View style={styles.container}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
      </View>
      <DraggableFlatList
        data={data}
        keyExtractor={(p, i) => p.uid ?? String(i)}
        renderItem={renderItem}
        onDragEnd={({ data: nd }) => reorder(pl.id, nd)}
        ListHeaderComponent={ListHeader}
        ListEmptyComponent={
          <View style={styles.empty}>
            <Text style={styles.emptyTitle}>{t('emptyPlaylistTitle')}</Text>
            <Text style={styles.emptyDesc}>{t('emptyPlaylistDesc')}</Text>
          </View>
        }
        ListFooterComponent={data.length > 0 ? <Text style={styles.hint}>{t('reorderHint')}</Text> : null}
        contentContainerStyle={{ paddingBottom: 80, paddingHorizontal: 12 }}
        activationDistance={15}
      />

      <Modal visible={renameOpen} transparent animationType="fade" onRequestClose={() => setRenameOpen(false)}>
        <Pressable style={renameStyles.backdrop} onPress={() => setRenameOpen(false)}>
          <Pressable style={renameStyles.sheet} onPress={() => {}}>
            <Text style={renameStyles.title}>{t('rename')}</Text>
            <TextInput
              style={renameStyles.input}
              value={nameInput}
              onChangeText={setNameInput}
              placeholder={pl.name}
              placeholderTextColor="rgba(250,250,247,0.4)"
              autoFocus
            />
            <Pressable
              style={renameStyles.save}
              onPress={() => { const n = nameInput.trim(); if (n) renamePlaylist(pl.id, n); setRenameOpen(false); }}
            >
              <Text style={renameStyles.saveText}>{t('save')}</Text>
            </Pressable>
          </Pressable>
        </Pressable>
      </Modal>
    </View>
  );
}

const renameStyles = StyleSheet.create({
  backdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.55)', justifyContent: 'center', padding: 28 },
  sheet: { backgroundColor: '#16181F', borderRadius: 20, padding: 18 },
  title: { fontSize: 16, fontWeight: '700', color: colors.text, marginBottom: 12 },
  input: {
    backgroundColor: 'rgba(255,255,255,0.06)', borderRadius: 12, paddingHorizontal: 14, paddingVertical: 12,
    fontSize: 16, color: colors.text,
  },
  save: { marginTop: 14, paddingVertical: 13, borderRadius: 14, backgroundColor: '#FCD34D', alignItems: 'center' },
  saveText: { fontSize: 14, fontWeight: '700', color: '#0A0A08' },
});

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 4 },
  titleArea: { paddingHorizontal: 8, paddingTop: 8, paddingBottom: 12 },
  title: { fontSize: 28, fontWeight: '800', color: colors.text, letterSpacing: -0.6 },
  subtitle: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 4 },
  playBig: {
    marginHorizontal: 8, marginBottom: 8, paddingVertical: 14, borderRadius: 16,
    backgroundColor: '#FAFAF7', flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8,
  },
  playBigText: { fontSize: 15, fontWeight: '700', color: '#0A0A08' },
  sectionLabel: {
    fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(250,250,247,0.45)',
    paddingHorizontal: 8, paddingTop: 18, paddingBottom: 10, textTransform: 'uppercase',
  },
  segment: { flexDirection: 'row', gap: 8, paddingHorizontal: 8 },
  segItem: { flex: 1, paddingVertical: 12, borderRadius: 14, alignItems: 'center', backgroundColor: 'rgba(255,255,255,0.06)' },
  segItemOn: { backgroundColor: colors.text },
  segText: { fontSize: 14, fontWeight: '600', color: colors.text },
  segTextOn: { color: '#0A0A08' },
  intervalGrid: { flexDirection: 'row', flexWrap: 'wrap', gap: 8, paddingHorizontal: 8 },
  chip: { paddingVertical: 10, paddingHorizontal: 16, borderRadius: 999, backgroundColor: 'rgba(255,255,255,0.06)' },
  chipOn: { backgroundColor: colors.text },
  chipText: { fontSize: 13, fontWeight: '600', color: colors.text },
  chipTextOn: { color: '#0A0A08' },
  row: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 9, paddingHorizontal: 8, backgroundColor: colors.bg },
  rowInfo: { flex: 1, minWidth: 0 },
  editBtn: { padding: 8, borderRadius: 10 },
  rowName: { fontSize: 15, fontWeight: '700', color: colors.text, letterSpacing: -0.2 },
  rowNameMissing: { color: 'rgba(250,250,247,0.4)' },
  rowMeta: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 2 },
  rowMetaMissing: { color: '#F59E0B' },
  coverMissing: { opacity: 0.35 },
  deleteAction: { width: 72, backgroundColor: '#DC2626', alignItems: 'center', justifyContent: 'center', marginVertical: 4, borderRadius: 12 },
  empty: { paddingHorizontal: 24, paddingTop: 20, alignItems: 'center' },
  emptyTitle: { fontSize: 16, fontWeight: '600', color: colors.text, marginBottom: 6 },
  emptyDesc: { fontSize: 13, color: 'rgba(250,250,247,0.5)', lineHeight: 19, textAlign: 'center' },
  hint: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.35)', textAlign: 'center', marginTop: 14 },
});
