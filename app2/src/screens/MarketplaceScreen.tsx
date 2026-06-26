import React, { useState, useEffect, useCallback, useMemo } from 'react';
import { View, Text, ScrollView, TextInput, Pressable, StyleSheet, FlatList, Alert, RefreshControl } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useMarketStore } from '../store/useMarketStore';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { uploadWasm, setMeta, setActiveProgram, deleteProgram } from '../ble/commands';
import { MarketItem } from '../types/marketplace';
import NavButton from '../components/NavButton';
import BleStatusPill from '../components/BleStatusPill';
import FeaturedCard from '../components/FeaturedCard';
import MarketRow from '../components/MarketRow';
import Skeleton from '../components/Skeleton';
import { BackIcon, SearchIcon } from '../components/Icon';
import { t, tCategory, localized } from '../i18n';
import { isVersionNewer } from '../utils/format';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'Marketplace'>;

export default function MarketplaceScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { catalog, featured, installedSlugs, loading, error, fetchCatalog, markInstalled } = useMarketStore();
  const { connectionState, deviceInfo } = useBleStore();
  const { addProgram, setActiveId, removeProgram } = useProgramStore();
  const programs = useProgramStore((s) => s.programs);
  const [category, setCategory] = useState('All');
  const [search, setSearch] = useState('');
  const [installingSlug, setInstallingSlug] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);
  const connected = connectionState === 'connected';

  const handleRefresh = useCallback(async () => {
    setRefreshing(true);
    try {
      await fetchCatalog();
    } finally {
      setRefreshing(false);
    }
  }, [fetchCatalog]);

  const categories = useMemo(() => {
    const cats = [...new Set(catalog.map((item) => item.category))].sort();
    return ['All', ...cats];
  }, [catalog]);

  useEffect(() => {
    if (catalog.length === 0) {
      fetchCatalog();
    }
  }, []);

  const quickInstall = useCallback(async (item: MarketItem) => {
    if (!connected) {
      Alert.alert(t('notConnected'), t('connectFirst'));
      return;
    }
    if (installingSlug) return;
    // If this slug is already installed, we're updating: remember the old id so
    // it can be removed once the new copy is uploaded.
    const existing = useProgramStore.getState().programs.find((p) => p.slug === item.slug);
    setInstallingSlug(item.slug);
    try {
      const response = await fetch(item.wasmUrl);
      if (!response.ok) throw new Error(`Download failed: ${response.status}`);
      const wasmData = new Uint8Array(await response.arrayBuffer());

      const newId = await uploadWasm(wasmData);

      await setMeta(newId, {
        name: item.name,
        desc: item.desc,
        author: item.author,
        category: item.category,
        cover: item.cover,
        pulse: item.pulse,
        tags: item.tags,
        slug: item.slug,
        version: item.version,
        guid: item.guid,
        i18n: item.i18n,
      });

      try {
        await setActiveProgram(newId);
        setActiveId(newId);
      } catch {}

      addProgram({
        id: newId,
        name: item.name,
        desc: item.desc,
        author: item.author,
        size: '',
        version: item.version,
        cover: item.cover,
        coverSvg: item.coverSvg,
        pulse: item.pulse,
        category: item.category,
        params: [],
        slug: item.slug,
        i18n: item.i18n,
      });

      // Updating: drop the previous copy on the device.
      if (existing && existing.id !== newId) {
        try { await deleteProgram(existing.id); removeProgram(existing.id); } catch {}
      }

      markInstalled(item.slug);
    } catch (err: any) {
      Alert.alert(t('installFailed'), err.message || t('unknownError'));
    } finally {
      setInstallingSlug(null);
    }
  }, [connected, installingSlug]);

  // A program is updatable when it's installed and the catalog has a version
  // that's newer than the installed one — or the installed version is unknown
  // (older installs that predate version-stamping).
  const isUpdatable = useCallback((item: MarketItem) => {
    if (!item.version) return false;
    const p = programs.find((pp) => pp.slug === item.slug);
    if (!p) return false;
    return p.version ? isVersionNewer(item.version, p.version) : true;
  }, [programs]);

  const filtered = catalog
    .filter((item) => {
      if (category !== 'All' && item.category !== category) return false;
      if (search && !item.name.toLowerCase().includes(search.toLowerCase()) && !item.author.toLowerCase().includes(search.toLowerCase())) return false;
      return true;
    })
    // Sort by the displayed (localized) name, same as the Library screen.
    .sort((a, b) =>
      localized(a, 'name', a.name).localeCompare(
        localized(b, 'name', b.name),
        undefined,
        { sensitivity: 'base' },
      ),
    );

  const featuredItems = catalog.filter((item) => featured.includes(item.slug));

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={{ paddingBottom: 60 }}
      refreshControl={
        <RefreshControl
          refreshing={refreshing}
          onRefresh={handleRefresh}
          tintColor={colors.text}
        />
      }
    >
      {/* Header */}
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <View style={styles.headerRow}>
          <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
          <BleStatusPill
            state={connectionState}
            name={deviceInfo.name}
            onPress={() => navigation.navigate('BleConnect')}
          />
        </View>
        <View style={styles.titleArea}>
          <Text style={styles.titleLabel}>github://ShadesLamp/programs</Text>
          <Text style={styles.title}>{t('marketplace')}</Text>
        </View>

        {/* Search */}
        <View style={styles.searchBar}>
          <SearchIcon size={20} color="rgba(250,250,247,0.5)" />
          <TextInput
            value={search}
            onChangeText={setSearch}
            placeholder={t('searchPlaceholder')}
            placeholderTextColor="rgba(250,250,247,0.35)"
            style={styles.searchInput}
          />
        </View>
      </View>

      {/* Loading skeletons */}
      {loading && (
        <View style={styles.skeletonList}>
          {[0, 1, 2, 3, 4, 5].map((i) => (
            <View key={i} style={styles.skeletonRow}>
              <Skeleton style={styles.skeletonCover} />
              <View style={styles.skeletonInfo}>
                <Skeleton style={styles.skeletonLine} />
                <Skeleton style={styles.skeletonLineShort} />
              </View>
              <Skeleton style={styles.skeletonBtn} />
            </View>
          ))}
        </View>
      )}

      {error && (
        <View style={styles.centered}>
          <Text style={styles.errorText}>{error}</Text>
          <Pressable onPress={fetchCatalog} style={styles.retryBtn}>
            <Text style={styles.retryText}>{t('retry')}</Text>
          </Pressable>
        </View>
      )}

      {!loading && !error && (
        <>
          {/* Featured carousel */}
          {category === 'All' && !search && featuredItems.length > 0 && (
            <>
              <View style={styles.featuredHeader}>
                <Text style={styles.featuredTitle}>{t('featured')}</Text>
                <Text style={styles.featuredMeta}>{t('programsCount', { n: catalog.length })}</Text>
              </View>
              <FlatList
                data={featuredItems}
                keyExtractor={(item) => item.slug}
                horizontal
                showsHorizontalScrollIndicator={false}
                contentContainerStyle={styles.featuredList}
                renderItem={({ item }) => (
                  <FeaturedCard
                    item={item}
                    installed={installedSlugs.includes(item.slug)}
                    onPress={() => navigation.navigate('MarketDetail', { itemId: item.slug })}
                  />
                )}
                ItemSeparatorComponent={() => <View style={{ width: 12 }} />}
              />
            </>
          )}

          {/* Category tabs */}
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.tabs}>
            {categories.map((cat) => (
              <Pressable
                key={cat}
                onPress={() => setCategory(cat)}
                style={[
                  styles.tab,
                  { backgroundColor: category === cat ? colors.text : 'rgba(255,255,255,0.06)' },
                ]}
              >
                <Text style={[styles.tabText, { color: category === cat ? '#0A0A08' : colors.text }]}>
                  {tCategory(cat)}
                </Text>
              </Pressable>
            ))}
          </ScrollView>

          {/* Results list */}
          <View style={styles.results}>
            {filtered.map((item) => (
              <MarketRow
                key={item.slug}
                item={item}
                installed={installedSlugs.includes(item.slug)}
                updatable={isUpdatable(item)}
                installing={installingSlug === item.slug}
                onPress={() => navigation.navigate('MarketDetail', { itemId: item.slug })}
                onAction={() => quickInstall(item)}
              />
            ))}
            {filtered.length === 0 && !loading && (
              <Text style={styles.noResults}>{t('noResults')}</Text>
            )}
          </View>
        </>
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
  },
  headerRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  titleArea: {
    marginTop: 14,
  },
  titleLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.45)',
    textTransform: 'uppercase',
  },
  title: {
    fontSize: 30,
    fontWeight: '800',
    color: colors.text,
    letterSpacing: -0.7,
    marginTop: 4,
  },
  searchBar: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 14,
    paddingVertical: 11,
    paddingHorizontal: 14,
    marginTop: 14,
  },
  searchInput: {
    flex: 1,
    color: colors.text,
    fontSize: 14,
    padding: 0,
  },
  centered: {
    alignItems: 'center',
    paddingVertical: 60,
    gap: 12,
  },
  skeletonList: {
    paddingHorizontal: 20,
    paddingTop: 12,
    gap: 8,
  },
  skeletonRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    backgroundColor: 'rgba(255,255,255,0.03)',
    borderRadius: 16,
    padding: 10,
  },
  skeletonCover: { width: 56, height: 56, borderRadius: 12 },
  skeletonInfo: { flex: 1, gap: 7 },
  skeletonLine: { height: 13, width: '55%', borderRadius: 6 },
  skeletonLineShort: { height: 11, width: '35%', borderRadius: 6 },
  skeletonBtn: { width: 36, height: 36, borderRadius: 18 },
  loadingText: {
    color: 'rgba(250,250,247,0.5)',
    fontSize: 13,
  },
  errorText: {
    color: '#F87171',
    fontSize: 13,
    textAlign: 'center',
    paddingHorizontal: 40,
  },
  retryBtn: {
    paddingVertical: 8,
    paddingHorizontal: 20,
    borderRadius: 12,
    backgroundColor: 'rgba(255,255,255,0.08)',
  },
  retryText: {
    color: colors.text,
    fontSize: 13,
    fontWeight: '600',
  },
  featuredHeader: {
    paddingHorizontal: 20,
    paddingTop: 14,
    paddingBottom: 10,
    flexDirection: 'row',
    alignItems: 'baseline',
    justifyContent: 'space-between',
  },
  featuredTitle: {
    fontSize: 17,
    fontWeight: '700',
    color: colors.text,
    letterSpacing: -0.3,
  },
  featuredMeta: {
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  featuredList: {
    paddingHorizontal: 20,
    paddingBottom: 4,
  },
  tabs: {
    paddingHorizontal: 20,
    paddingTop: 20,
    paddingBottom: 6,
    gap: 8,
  },
  tab: {
    paddingVertical: 8,
    paddingHorizontal: 16,
    borderRadius: 999,
  },
  tabText: {
    fontSize: 13,
    fontWeight: '600',
    letterSpacing: -0.1,
  },
  results: {
    paddingHorizontal: 20,
    paddingTop: 12,
    gap: 8,
  },
  noResults: {
    textAlign: 'center',
    padding: 40,
    color: 'rgba(250,250,247,0.4)',
    fontSize: 13,
  },
});
