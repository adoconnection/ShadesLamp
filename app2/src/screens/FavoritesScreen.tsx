import React, { useMemo } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { Swipeable } from 'react-native-gesture-handler';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useProgramStore } from '../store/useProgramStore';
import { useFavoritesStore } from '../store/useFavoritesStore';
import { FavoriteDisplay, buildFavoriteList } from '../utils/favorites';
import { applyVariant } from '../ble/applyVariant';
import Cover from '../components/Cover';
import NavButton from '../components/NavButton';
import { BackIcon, MarketIcon, StarFillIcon, StarOutlineIcon, ChevronIcon, TrashIcon } from '../components/Icon';
import { t } from '../i18n';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'Favorites'>;

function formatInterval(sec: number): string {
  if (sec >= 60 && sec % 60 === 0) return t('minShort', { n: sec / 60 });
  return t('secShort', { n: sec });
}

export default function FavoritesScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { programs, activeId } = useProgramStore();
  const variants = useFavoritesStore((s) => s.variants);
  const removeVariant = useFavoritesStore((s) => s.removeVariant);
  const rotationMode = useFavoritesStore((s) => s.rotationMode);
  const rotationIntervalSec = useFavoritesStore((s) => s.rotationIntervalSec);

  const items = useMemo(() => buildFavoriteList(variants, programs), [variants, programs]);

  const rotationLabel =
    rotationMode === 'off' ? t('rotationOff')
    : rotationMode === 'random' ? t('rotationRandom')
    : t('rotationNext');

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 60 }}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        <Text style={styles.headerCount}>{t('savedCount', { n: items.length })}</Text>
      </View>

      {/* Hero */}
      <View style={styles.heroWrap}>
        <View style={styles.hero}>
          <LinearGradient
            colors={['#FCD34D', '#F59E0B', '#B45309']}
            start={{ x: 0, y: 0 }}
            end={{ x: 1, y: 1 }}
            style={StyleSheet.absoluteFill}
          />
          <View style={styles.heroRow}>
            <View style={styles.starCircle}>
              <StarFillIcon size={22} color="#FAFAF7" />
            </View>
            <View>
              <Text style={styles.heroLabel}>{t('yourCollection')}</Text>
              <Text style={styles.heroTitle}>{t('favorites')}</Text>
            </View>
          </View>
          <Text style={styles.heroDesc}>{t('favHeroDesc2')}</Text>
        </View>
      </View>

      {/* Rotation entry */}
      <Pressable style={styles.rotationCard} onPress={() => navigation.navigate('RotationSettings')}>
        <View style={[styles.rotationDot, rotationMode !== 'off' && styles.rotationDotOn]} />
        <View style={{ flex: 1 }}>
          <Text style={styles.rotationTitle}>{t('rotation')}</Text>
          <Text style={styles.rotationSub}>
            {rotationMode === 'off'
              ? t('rotationOffDesc')
              : `${rotationLabel} · ${t('rotationOnDesc', { t: formatInterval(rotationIntervalSec) })}`}
          </Text>
        </View>
        <ChevronIcon size={16} color="rgba(250,250,247,0.4)" />
      </Pressable>

      {items.length === 0 ? (
        <View style={styles.empty}>
          <View style={styles.emptyCircle}>
            <StarOutlineIcon size={32} color="#FCD34D" />
          </View>
          <Text style={styles.emptyTitle}>{t('noFavoritesTitle')}</Text>
          <Text style={styles.emptyDesc}>{t('noFavoritesDesc2')}</Text>
          <Pressable onPress={() => navigation.navigate('Marketplace')} style={styles.browseBtn}>
            <MarketIcon size={18} color={colors.text} />
            <Text style={styles.browseText}>{t('browseMarketplace')}</Text>
          </Pressable>
        </View>
      ) : (
        <View style={styles.list}>
          {items.map((d) => (
            <FavoriteRow
              key={d.v.key}
              d={d}
              active={resolveActive(d, programs, activeId)}
              onTap={() => applyVariant(d.v)}
              onDelete={() => removeVariant(d.v.key)}
            />
          ))}
          <Text style={styles.hint}>{t('swipeToDelete')}</Text>
        </View>
      )}
    </ScrollView>
  );
}

function resolveActive(d: FavoriteDisplay, programs: { id: number; slug?: string }[], activeId: number): boolean {
  const p = d.v.slug ? programs.find((x) => x.slug === d.v.slug) : programs.find((x) => x.id === d.v.programId);
  return (p ? p.id : d.v.programId) === activeId;
}

function FavoriteRow({ d, active, onTap, onDelete }: {
  d: FavoriteDisplay; active: boolean; onTap: () => void; onDelete: () => void;
}) {
  return (
    <Swipeable
      renderRightActions={() => (
        <View style={styles.deleteAction}>
          <TrashIcon size={18} color="#FAFAF7" />
        </View>
      )}
      rightThreshold={40}
      onSwipeableOpen={onDelete}
    >
      <Pressable style={styles.row} onPress={onTap}>
        <Cover cover={d.cover} pulse={d.pulse} size={48} radius={12} animated={active} />
        <View style={styles.rowInfo}>
          <Text style={[styles.rowName, active && { color: d.pulse }]} numberOfLines={1}>{d.label}</Text>
          <Text style={styles.rowMeta} numberOfLines={1}>
            {d.v.params.length > 0 ? `${d.v.params.length} params` : ''}
          </Text>
        </View>
      </Pressable>
    </Swipeable>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: {
    paddingHorizontal: 20, paddingBottom: 12,
    flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center',
  },
  headerCount: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)' },
  heroWrap: { paddingHorizontal: 20, paddingTop: 8, paddingBottom: 16 },
  hero: { borderRadius: 28, overflow: 'hidden', padding: 28, paddingHorizontal: 22, minHeight: 140 },
  heroRow: { flexDirection: 'row', alignItems: 'center', gap: 16 },
  starCircle: {
    width: 56, height: 56, borderRadius: 18, backgroundColor: 'rgba(0,0,0,0.25)',
    alignItems: 'center', justifyContent: 'center',
  },
  heroLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(0,0,0,0.65)', textTransform: 'uppercase' },
  heroTitle: { fontSize: 30, fontWeight: '800', color: '#0A0A08', letterSpacing: -0.7, marginTop: 2 },
  heroDesc: { fontSize: 13, color: 'rgba(0,0,0,0.7)', marginTop: 14, lineHeight: 19 },
  rotationCard: {
    marginHorizontal: 20, marginBottom: 18, padding: 16, borderRadius: 18,
    backgroundColor: 'rgba(255,255,255,0.04)', borderWidth: 0.5, borderColor: 'rgba(255,255,255,0.06)',
    flexDirection: 'row', alignItems: 'center', gap: 14,
  },
  rotationDot: { width: 10, height: 10, borderRadius: 5, backgroundColor: 'rgba(250,250,247,0.25)' },
  rotationDotOn: { backgroundColor: colors.green },
  rotationTitle: { fontSize: 15, fontWeight: '700', color: colors.text, letterSpacing: -0.2 },
  rotationSub: { fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 2 },
  empty: { paddingHorizontal: 24, paddingTop: 30, alignItems: 'center' },
  emptyCircle: {
    width: 72, height: 72, borderRadius: 36, backgroundColor: 'rgba(252,211,77,0.1)',
    alignItems: 'center', justifyContent: 'center', marginBottom: 16,
  },
  emptyTitle: { fontSize: 16, fontWeight: '600', color: colors.text, marginBottom: 6 },
  emptyDesc: { fontSize: 13, color: 'rgba(250,250,247,0.5)', lineHeight: 19, textAlign: 'center', marginBottom: 18 },
  browseBtn: {
    backgroundColor: 'rgba(255,255,255,0.06)', borderRadius: 999, paddingVertical: 11, paddingHorizontal: 18,
    flexDirection: 'row', alignItems: 'center', gap: 8,
  },
  browseText: { fontSize: 13, fontWeight: '600', color: colors.text },
  list: { paddingHorizontal: 12 },
  row: {
    flexDirection: 'row', alignItems: 'center', gap: 12,
    paddingVertical: 10, paddingHorizontal: 8, backgroundColor: colors.bg,
  },
  rowInfo: { flex: 1, minWidth: 0 },
  rowName: { fontSize: 16, fontWeight: '700', color: colors.text, letterSpacing: -0.3 },
  rowMeta: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 2 },
  deleteAction: {
    width: 72, backgroundColor: '#DC2626', alignItems: 'center', justifyContent: 'center',
    marginVertical: 4, borderRadius: 12,
  },
  hint: {
    fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.35)',
    textAlign: 'center', marginTop: 14,
  },
});
