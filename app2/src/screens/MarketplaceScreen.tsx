import React, { useState, useEffect, useCallback } from 'react';
import { View, Text, ScrollView, TextInput, Pressable, StyleSheet, FlatList, ActivityIndicator, Alert } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useMarketStore } from '../store/useMarketStore';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { uploadWasm, setMeta, setActiveProgram } from '../ble/commands';
import { MarketItem } from '../types/marketplace';
import NavButton from '../components/NavButton';
import BleStatusPill from '../components/BleStatusPill';
import FeaturedCard from '../components/FeaturedCard';
import MarketRow from '../components/MarketRow';
import { BackIcon, SearchIcon } from '../components/Icon';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

const CATEGORIES = ['All', 'Effects', 'Ambient', 'Games', 'Visualizers'] as const;

type Props = NativeStackScreenProps<RootStackParamList, 'Marketplace'>;

export default function MarketplaceScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { catalog, featured, installedSlugs, loading, error, fetchCatalog, markInstalled } = useMarketStore();
  const { connectionState, deviceInfo } = useBleStore();
  const { addProgram, setActiveId } = useProgramStore();
  const [category, setCategory] = useState('All');
  const [search, setSearch] = useState('');
  const [installingSlug, setInstallingSlug] = useState<string | null>(null);
  const connected = connectionState === 'connected';

  useEffect(() => {
    if (catalog.length === 0) {
      fetchCatalog();
    }
  }, []);

  const quickInstall = useCallback(async (item: MarketItem) => {
    if (!connected) {
      Alert.alert('Not connected', 'Connect to a device first.');
      return;
    }
    if (installingSlug) return;
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
        cover: item.cover,
        coverSvg: item.coverSvg,
        pulse: item.pulse,
        category: item.category,
        params: [],
        slug: item.slug,
      });

      markInstalled(item.slug);
    } catch (err: any) {
      Alert.alert('Install failed', err.message || 'Unknown error');
    } finally {
      setInstallingSlug(null);
    }
  }, [connected, installingSlug]);

  const filtered = catalog.filter((item) => {
    if (category !== 'All' && item.category !== category) return false;
    if (search && !item.name.toLowerCase().includes(search.toLowerCase()) && !item.author.toLowerCase().includes(search.toLowerCase())) return false;
    return true;
  });

  const featuredItems = catalog.filter((item) => featured.includes(item.slug));

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ paddingBottom: 60 }}>
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
          <Text style={styles.title}>Marketplace</Text>
        </View>

        {/* Search */}
        <View style={styles.searchBar}>
          <SearchIcon size={20} color="rgba(250,250,247,0.5)" />
          <TextInput
            value={search}
            onChangeText={setSearch}
            placeholder="Search programs, authors..."
            placeholderTextColor="rgba(250,250,247,0.35)"
            style={styles.searchInput}
          />
        </View>
      </View>

      {/* Loading / Error */}
      {loading && (
        <View style={styles.centered}>
          <ActivityIndicator color={colors.text} size="small" />
          <Text style={styles.loadingText}>Loading catalog...</Text>
        </View>
      )}

      {error && (
        <View style={styles.centered}>
          <Text style={styles.errorText}>{error}</Text>
          <Pressable onPress={fetchCatalog} style={styles.retryBtn}>
            <Text style={styles.retryText}>Retry</Text>
          </Pressable>
        </View>
      )}

      {!loading && !error && (
        <>
          {/* Featured carousel */}
          {category === 'All' && !search && featuredItems.length > 0 && (
            <>
              <View style={styles.featuredHeader}>
                <Text style={styles.featuredTitle}>Featured</Text>
                <Text style={styles.featuredMeta}>{catalog.length} programs</Text>
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
            {CATEGORIES.map((cat) => (
              <Pressable
                key={cat}
                onPress={() => setCategory(cat)}
                style={[
                  styles.tab,
                  { backgroundColor: category === cat ? colors.text : 'rgba(255,255,255,0.06)' },
                ]}
              >
                <Text style={[styles.tabText, { color: category === cat ? '#0A0A08' : colors.text }]}>
                  {cat}
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
                installing={installingSlug === item.slug}
                onPress={() => navigation.navigate('MarketDetail', { itemId: item.slug })}
                onAction={() => quickInstall(item)}
              />
            ))}
            {filtered.length === 0 && !loading && (
              <Text style={styles.noResults}>No programs match this filter.</Text>
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
