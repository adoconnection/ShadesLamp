import React, { useEffect } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet, Alert } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { Swipeable } from 'react-native-gesture-handler';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { Playlist } from '../types/playlist';
import { useProgramStore } from '../store/useProgramStore';
import { usePlaylistStore } from '../store/usePlaylistStore';
import { useBleStore } from '../store/useBleStore';
import { gradientColors } from '../utils/color';
import Cover from '../components/Cover';
import NavButton from '../components/NavButton';
import { BackIcon, StarFillIcon, TrashIcon, ChevronIcon, PlusIcon, PlayIcon, PauseIcon } from '../components/Icon';
import { t } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'Favorites'>;

export default function FavoritesScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { programs } = useProgramStore();
  const playlists = usePlaylistStore((s) => s.playlists);
  const playingId = usePlaylistStore((s) => s.playingId);
  const { load, createPlaylist, deletePlaylist, play, stop } = usePlaylistStore();
  const connected = useBleStore((s) => s.connectionState === 'connected');

  useEffect(() => { load(); }, [load]);

  function coverFor(pl: Playlist) {
    const first = pl.positions[0];
    if (first) {
      const prog = first.slug ? programs.find((p) => p.slug === first.slug) : programs.find((p) => p.id === first.prog);
      if (prog) return { cover: prog.cover, pulse: prog.pulse };
    }
    return { cover: { from: '#FCD34D', to: '#B45309', angle: 135 }, pulse: '#FCD34D' };
  }

  const handleCreate = async () => {
    if (!connected) { Alert.alert(t('connectToManage')); return; }
    const id = await createPlaylist(t('playlistDefaultName', { n: playlists.length + 1 }));
    if (id != null) navigation.navigate('PlaylistDetail', { playlistId: id });
  };

  const handleDelete = (pl: Playlist) => {
    Alert.alert(t('deletePlaylistTitle'), t('deletePlaylistMsg'), [
      { text: t('cancel'), style: 'cancel' },
      { text: t('delete'), style: 'destructive', onPress: () => deletePlaylist(pl.id) },
    ]);
  };

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 60 }}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        <Text style={styles.headerCount}>{t('savedCount', { n: playlists.length })}</Text>
      </View>

      {/* Hero */}
      <View style={styles.heroWrap}>
        <View style={styles.hero}>
          <LinearGradient colors={['#FCD34D', '#F59E0B', '#B45309']} start={{ x: 0, y: 0 }} end={{ x: 1, y: 1 }} style={StyleSheet.absoluteFill} />
          <View style={styles.heroRow}>
            <View style={styles.starCircle}><StarFillIcon size={22} color="#FAFAF7" /></View>
            <View>
              <Text style={styles.heroLabel}>{t('yourCollection')}</Text>
              <Text style={styles.heroTitle}>{t('playlists')}</Text>
            </View>
          </View>
          <Text style={styles.heroDesc}>{t('emptyPlaylistsDesc')}</Text>
        </View>
      </View>

      {/* Create */}
      <Pressable style={styles.createBtn} onPress={handleCreate}>
        <PlusIcon size={18} color={colors.text} />
        <Text style={styles.createText}>{t('newPlaylist')}</Text>
      </Pressable>

      {playlists.length === 0 ? (
        <View style={styles.empty}>
          <Text style={styles.emptyTitle}>{t('emptyPlaylistsTitle')}</Text>
          <Text style={styles.emptyDesc}>{connected ? t('emptyPlaylistsDesc') : t('connectToManage')}</Text>
        </View>
      ) : (
        <View style={styles.list}>
          {playlists.map((pl) => {
            const cd = coverFor(pl);
            const isPlaying = playingId === pl.id;
            return (
              <Swipeable
                key={pl.id}
                renderRightActions={() => (
                  <View style={styles.deleteAction}><TrashIcon size={18} color="#FAFAF7" /></View>
                )}
                rightThreshold={40}
                onSwipeableOpen={() => handleDelete(pl)}
              >
                <Pressable style={styles.row} onPress={() => navigation.navigate('PlaylistDetail', { playlistId: pl.id })}>
                  <Cover cover={cd.cover} pulse={cd.pulse} size={48} radius={12} animated={isPlaying} />
                  <View style={styles.rowInfo}>
                    <Text style={[styles.rowName, isPlaying && { color: cd.pulse }]} numberOfLines={1}>{pl.name}</Text>
                    <Text style={styles.rowMeta} numberOfLines={1}>{t('positionsCount', { n: pl.positions.length })}</Text>
                  </View>
                  <Pressable
                    hitSlop={10}
                    style={[styles.playBtn, isPlaying && { backgroundColor: cd.pulse }]}
                    onPress={() => (isPlaying ? stop() : play(pl.id))}
                  >
                    {isPlaying ? <PauseIcon size={16} color="#0A0A08" /> : <PlayIcon size={16} color={colors.text} />}
                  </Pressable>
                  <ChevronIcon size={16} color="rgba(250,250,247,0.4)" />
                </Pressable>
              </Swipeable>
            );
          })}
          <Text style={styles.hint}>{t('reorderHint')}</Text>
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { paddingHorizontal: 20, paddingBottom: 12, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  headerCount: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)' },
  heroWrap: { paddingHorizontal: 20, paddingTop: 8, paddingBottom: 16 },
  hero: { borderRadius: 28, overflow: 'hidden', padding: 28, paddingHorizontal: 22, minHeight: 140 },
  heroRow: { flexDirection: 'row', alignItems: 'center', gap: 16 },
  starCircle: { width: 56, height: 56, borderRadius: 18, backgroundColor: 'rgba(0,0,0,0.25)', alignItems: 'center', justifyContent: 'center' },
  heroLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(0,0,0,0.65)', textTransform: 'uppercase' },
  heroTitle: { fontSize: 30, fontWeight: '800', color: '#0A0A08', letterSpacing: -0.7, marginTop: 2 },
  heroDesc: { fontSize: 13, color: 'rgba(0,0,0,0.7)', marginTop: 14, lineHeight: 19 },
  createBtn: {
    marginHorizontal: 20, marginBottom: 16, paddingVertical: 13, borderRadius: 16,
    backgroundColor: 'rgba(255,255,255,0.06)', flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8,
  },
  createText: { fontSize: 14, fontWeight: '700', color: colors.text },
  empty: { paddingHorizontal: 24, paddingTop: 10, alignItems: 'center' },
  emptyTitle: { fontSize: 16, fontWeight: '600', color: colors.text, marginBottom: 6 },
  emptyDesc: { fontSize: 13, color: 'rgba(250,250,247,0.5)', lineHeight: 19, textAlign: 'center' },
  list: { paddingHorizontal: 12 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 10, paddingHorizontal: 8, backgroundColor: colors.bg },
  rowInfo: { flex: 1, minWidth: 0 },
  rowName: { fontSize: 16, fontWeight: '700', color: colors.text, letterSpacing: -0.3 },
  rowMeta: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 2 },
  playBtn: { width: 34, height: 34, borderRadius: 17, backgroundColor: 'rgba(255,255,255,0.08)', alignItems: 'center', justifyContent: 'center' },
  deleteAction: { width: 72, backgroundColor: '#DC2626', alignItems: 'center', justifyContent: 'center', marginVertical: 4, borderRadius: 12 },
  hint: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.35)', textAlign: 'center', marginTop: 14 },
});
