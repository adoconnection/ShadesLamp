import React from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useProgramStore } from '../store/useProgramStore';
import { useFavoritesStore } from '../store/useFavoritesStore';
import NavButton from '../components/NavButton';
import ProgramRow from '../components/ProgramRow';
import { BackIcon, MarketIcon, StarFillIcon, StarOutlineIcon } from '../components/Icon';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'Favorites'>;

export default function FavoritesScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { programs, activeId, setActiveId } = useProgramStore();
  const { favorites } = useFavoritesStore();
  const items = programs.filter((p) => favorites.includes(p.id));

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 60 }}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
        <Text style={styles.headerCount}>{items.length} starred</Text>
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
              <Text style={styles.heroLabel}>YOUR COLLECTION</Text>
              <Text style={styles.heroTitle}>Favorites</Text>
            </View>
          </View>
          <Text style={styles.heroDesc}>
            Quick access to your starred programs. Tap the star on any program to add it here.
          </Text>
        </View>
      </View>

      {items.length === 0 ? (
        <View style={styles.empty}>
          <View style={styles.emptyCircle}>
            <StarOutlineIcon size={32} color="#FCD34D" />
          </View>
          <Text style={styles.emptyTitle}>No favorites yet</Text>
          <Text style={styles.emptyDesc}>
            Open any program and tap the star to pin it here for quick access.
          </Text>
          <Pressable
            onPress={() => navigation.navigate('Marketplace')}
            style={styles.browseBtn}
          >
            <MarketIcon size={18} color={colors.text} />
            <Text style={styles.browseText}>Browse marketplace</Text>
          </Pressable>
        </View>
      ) : (
        <View style={styles.list}>
          {items.map((p) => (
            <ProgramRow
              key={p.id}
              program={p}
              active={p.id === activeId}
              onTap={() => setActiveId(p.id)}
              onOpen={() => navigation.navigate('ProgramDetail', { programId: p.id })}
            />
          ))}
        </View>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: colors.bg,
  },
  header: {
    paddingHorizontal: 20,
    paddingBottom: 12,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  headerCount: {
    fontFamily: fonts.mono,
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  heroWrap: {
    paddingHorizontal: 20,
    paddingTop: 8,
    paddingBottom: 20,
  },
  hero: {
    borderRadius: 28,
    overflow: 'hidden',
    padding: 28,
    paddingHorizontal: 22,
    minHeight: 140,
  },
  heroRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 16,
  },
  starCircle: {
    width: 56,
    height: 56,
    borderRadius: 18,
    backgroundColor: 'rgba(0,0,0,0.25)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  heroLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(0,0,0,0.65)',
    textTransform: 'uppercase',
  },
  heroTitle: {
    fontSize: 30,
    fontWeight: '800',
    color: '#0A0A08',
    letterSpacing: -0.7,
    marginTop: 2,
  },
  heroDesc: {
    fontSize: 13,
    color: 'rgba(0,0,0,0.7)',
    marginTop: 14,
    lineHeight: 19,
  },
  empty: {
    paddingHorizontal: 24,
    paddingTop: 30,
    alignItems: 'center',
  },
  emptyCircle: {
    width: 72,
    height: 72,
    borderRadius: 36,
    backgroundColor: 'rgba(252,211,77,0.1)',
    alignItems: 'center',
    justifyContent: 'center',
    marginBottom: 16,
  },
  emptyTitle: {
    fontSize: 16,
    fontWeight: '600',
    color: colors.text,
    marginBottom: 6,
  },
  emptyDesc: {
    fontSize: 13,
    color: 'rgba(250,250,247,0.5)',
    lineHeight: 19,
    textAlign: 'center',
    marginBottom: 18,
  },
  browseBtn: {
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 999,
    paddingVertical: 11,
    paddingHorizontal: 18,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  browseText: {
    fontSize: 13,
    fontWeight: '600',
    color: colors.text,
  },
  list: {
    paddingHorizontal: 12,
  },
});
