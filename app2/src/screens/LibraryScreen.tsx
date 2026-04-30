import React, { useCallback, useEffect, useState, useMemo } from 'react';
import { View, Text, Pressable, ScrollView, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  Easing,
} from 'react-native-reanimated';
import DraggableFlatList, { RenderItemParams, ScaleDecorator } from 'react-native-draggable-flatlist';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { Program } from '../types/program';
import { useProgramStore } from '../store/useProgramStore';
import { useFavoritesStore } from '../store/useFavoritesStore';
import { useBleStore } from '../store/useBleStore';
import { setActiveProgram, setPower, setOrder } from '../ble/commands';
import { refreshPrograms } from '../ble/connectFlow';
import BleStatusPill from '../components/BleStatusPill';
import ProgramRow from '../components/ProgramRow';
import ActionTile from '../components/ActionTile';
import { MarketIcon, StarOutlineIcon, SettingsIcon, PowerIcon } from '../components/Icon';
import { gradientColors } from '../utils/color';
import { padId } from '../utils/format';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'Library'>;

export default function LibraryScreen({ navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { programs, activeId, setActiveId, reorderPrograms } = useProgramStore();
  const { favorites } = useFavoritesStore();
  const { connectionState, deviceInfo, powerOn, setPowerOn } = useBleStore();
  const [category, setCategory] = useState('All');
  const [refreshing, setRefreshing] = useState(false);

  const handleRefresh = useCallback(async () => {
    if (connectionState !== 'connected') return;
    setRefreshing(true);
    try {
      await refreshPrograms();
    } catch (err) {
      console.warn('Refresh failed:', err);
    } finally {
      setRefreshing(false);
    }
  }, [connectionState]);

  const categories = useMemo(() => {
    const cats = [...new Set(programs.map((p) => p.category))].sort();
    return ['All', ...cats];
  }, [programs]);

  const sortedAndFiltered = useMemo(() => {
    const sorted = programs.slice().sort((a, b) =>
      a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }),
    );
    if (category === 'All') return sorted;
    return sorted.filter((p) => p.category === category);
  }, [programs, category]);

  const activeProgram = programs.find((p) => p.id === activeId);

  const handleActivate = useCallback(async (id: number) => {
    if (connectionState === 'connected') {
      try {
        await setActiveProgram(id);
      } catch (err) {
        console.warn('Failed to activate program via BLE:', err);
      }
    }
    setActiveId(id);
  }, [connectionState, setActiveId]);

  const handlePowerToggle = useCallback(async () => {
    const newState = !powerOn;
    setPowerOn(newState);
    if (connectionState === 'connected') {
      try {
        await setPower(newState);
      } catch (err) {
        console.warn('Failed to toggle power via BLE:', err);
        setPowerOn(!newState); // revert on failure
      }
    }
  }, [connectionState, powerOn, setPowerOn]);

  const handleDragEnd = useCallback(({ data }: { data: Program[] }) => {
    reorderPrograms(data);
    if (connectionState === 'connected') {
      setOrder(data.map((p) => p.id)).catch(() => {});
    }
  }, [connectionState, reorderPrograms]);

  const canDrag = category === 'All';

  const renderItem = useCallback(({ item: p, drag, isActive: isDragging }: RenderItemParams<Program>) => (
    <ScaleDecorator>
      <View style={styles.listItem}>
        <ProgramRow
          program={p}
          active={p.id === activeId}
          isFavorite={favorites.includes(p.id)}
          onTap={() => handleActivate(p.id)}
          onOpen={() => navigation.navigate('ProgramDetail', { programId: p.id })}
          onLongPress={canDrag ? drag : undefined}
          isDragging={isDragging}
        />
      </View>
    </ScaleDecorator>
  ), [activeId, favorites, handleActivate, navigation, canDrag]);

  const keyExtractor = useCallback((p: Program) => String(p.id), []);

  const ListHeader = (
    <>
      {/* Header */}
      <View style={[styles.header, { paddingTop: insets.top + 12 }]}>
        <View>
          <Text style={styles.headerLabel}>SHADES</Text>
          <Text style={styles.headerTitle}>Library</Text>
        </View>
        <View style={styles.headerRight}>
          <Pressable
            onPress={handlePowerToggle}
            style={[styles.powerBtn, !powerOn && styles.powerBtnOff]}
            hitSlop={8}
          >
            <PowerIcon size={18} color={powerOn ? '#4ADE80' : 'rgba(250,250,247,0.4)'} />
          </Pressable>
          <BleStatusPill
            state={connectionState}
            name={deviceInfo.name}
            onPress={() => navigation.navigate('BleConnect')}
          />
        </View>
      </View>

      {/* Now Playing Hero */}
      {activeProgram && (
        <Pressable
          onPress={() => navigation.navigate('ProgramDetail', { programId: activeProgram.id })}
          style={styles.heroWrap}
        >
          <View style={[styles.hero, {
            shadowColor: activeProgram.pulse,
            shadowOffset: { width: 0, height: 20 },
            shadowOpacity: 0.2,
            shadowRadius: 25,
            elevation: 12,
          }]}>
            <LinearGradient
              colors={gradientColors(activeProgram.cover)}
              start={{ x: 0, y: 0 }}
              end={{ x: 1, y: 1 }}
              style={StyleSheet.absoluteFill}
            />
            <HeroOverlay
              color={activeProgram.cover.via || activeProgram.cover.from}
              startX={0.3}
              startY={0.2}
              duration={6000}
            />
            <HeroOverlay
              color={activeProgram.cover.to}
              startX={0.8}
              startY={0.8}
              duration={8000}
            />
            <View style={styles.heroContent}>
              <View style={styles.heroTop}>
                <View style={styles.nowRunning}>
                  <View style={styles.nowDot} />
                  <Text style={styles.nowLabel}>NOW RUNNING</Text>
                </View>
                <View style={styles.idBadge}>
                  <Text style={styles.idText}>ID {padId(activeProgram.id)}</Text>
                </View>
              </View>

              <View style={styles.heroBottom}>
                <Text style={styles.heroTitle}>{activeProgram.name}</Text>
                <Text style={styles.heroDesc}>{activeProgram.desc}</Text>
                <View style={styles.paramChips}>
                  {activeProgram.params.slice(0, 3).map((p) => (
                    <View key={p.id} style={styles.chip}>
                      <Text style={styles.chipLabel}>{p.name}</Text>
                      <Text style={styles.chipValue}>
                        {p.type === 'bool'
                          ? p.value ? 'on' : 'off'
                          : p.type === 'select' && p.options
                          ? p.options[p.value]
                          : p.type === 'float'
                          ? p.value.toFixed(2)
                          : p.value}
                      </Text>
                    </View>
                  ))}
                </View>
              </View>
            </View>
          </View>
        </Pressable>
      )}

      {/* Quick Actions */}
      <View style={styles.actions}>
        <ActionTile
          icon={<MarketIcon color="rgba(250,250,247,0.85)" />}
          label="Marketplace"
          onPress={() => navigation.navigate('Marketplace')}
        />
        <ActionTile
          icon={<StarOutlineIcon color="rgba(250,250,247,0.85)" />}
          label="Favorites"
          detail={favorites.length > 0 ? String(favorites.length) : null}
          onPress={() => navigation.navigate('Favorites')}
        />
        <ActionTile
          icon={<SettingsIcon color="rgba(250,250,247,0.85)" />}
          label="Device"
          onPress={() => navigation.navigate('DeviceSettings')}
        />
      </View>

      {/* Section header */}
      <View style={styles.sectionHeader}>
        <Text style={styles.sectionTitle}>Installed</Text>
        <Text style={styles.sectionCount}>{programs.length} / 128</Text>
      </View>

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
              {cat}
            </Text>
          </Pressable>
        ))}
      </ScrollView>
    </>
  );

  return (
    <DraggableFlatList
      data={sortedAndFiltered}
      keyExtractor={keyExtractor}
      renderItem={renderItem}
      onDragEnd={handleDragEnd}
      ListHeaderComponent={ListHeader}
      contentContainerStyle={{ paddingBottom: 80 }}
      containerStyle={styles.container}
      activationDistance={15}
      refreshing={refreshing}
      onRefresh={connectionState === 'connected' ? handleRefresh : undefined}
    />
  );
}

function HeroOverlay({ color, startX, startY, duration }: {
  color: string; startX: number; startY: number; duration: number;
}) {
  const translateX = useSharedValue(0);
  const translateY = useSharedValue(0);

  useEffect(() => {
    translateX.value = withRepeat(
      withTiming(30, { duration, easing: Easing.inOut(Easing.ease) }),
      -1,
      true,
    );
    translateY.value = withRepeat(
      withTiming(20, { duration: duration * 0.8, easing: Easing.inOut(Easing.ease) }),
      -1,
      true,
    );
  }, []);

  const animStyle = useAnimatedStyle(() => ({
    transform: [
      { translateX: translateX.value },
      { translateY: translateY.value },
    ],
  }));

  return (
    <Animated.View
      style={[
        StyleSheet.absoluteFill,
        {
          backgroundColor: color,
          opacity: 0.35,
          borderRadius: 999,
          top: `${startY * 100 - 30}%`,
          left: `${startX * 100 - 30}%`,
          width: '60%',
          height: '60%',
        },
        animStyle,
      ]}
    />
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
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  headerLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.45)',
    textTransform: 'uppercase',
  },
  headerTitle: {
    fontSize: 22,
    fontWeight: '700',
    color: colors.text,
    letterSpacing: -0.5,
    marginTop: 1,
  },
  headerRight: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
  },
  powerBtn: {
    width: 36,
    height: 36,
    borderRadius: 18,
    backgroundColor: 'rgba(74,222,128,0.12)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  powerBtnOff: {
    backgroundColor: 'rgba(250,250,247,0.06)',
  },
  heroWrap: {
    paddingHorizontal: 20,
    paddingTop: 8,
    paddingBottom: 24,
  },
  hero: {
    borderRadius: 28,
    overflow: 'hidden',
    minHeight: 220,
  },
  heroContent: {
    flex: 1,
    padding: 20,
    paddingBottom: 18,
    justifyContent: 'space-between',
  },
  heroTop: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'flex-start',
  },
  nowRunning: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  nowDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: '#FAFAF7',
  },
  nowLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(255,255,255,0.85)',
    textTransform: 'uppercase',
  },
  idBadge: {
    backgroundColor: 'rgba(0,0,0,0.25)',
    borderRadius: 999,
    paddingVertical: 5,
    paddingHorizontal: 10,
  },
  idText: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: '#FAFAF7',
  },
  heroBottom: {
    marginTop: 40,
  },
  heroTitle: {
    fontSize: 32,
    fontWeight: '800',
    color: '#FAFAF7',
    letterSpacing: -1,
    lineHeight: 34,
  },
  heroDesc: {
    fontSize: 13,
    color: 'rgba(255,255,255,0.85)',
    marginTop: 4,
  },
  paramChips: {
    flexDirection: 'row',
    gap: 10,
    marginTop: 14,
  },
  chip: {
    backgroundColor: 'rgba(0,0,0,0.3)',
    borderRadius: 10,
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderWidth: 0.5,
    borderColor: 'rgba(255,255,255,0.18)',
    flexDirection: 'row',
    gap: 6,
  },
  chipLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: 'rgba(255,255,255,0.65)',
  },
  chipValue: {
    fontFamily: fonts.mono,
    fontSize: 11,
    fontWeight: '600',
    color: '#FAFAF7',
  },
  actions: {
    paddingHorizontal: 20,
    paddingBottom: 18,
    flexDirection: 'row',
    gap: 10,
  },
  sectionHeader: {
    paddingHorizontal: 20,
    paddingTop: 4,
    paddingBottom: 10,
    flexDirection: 'row',
    alignItems: 'baseline',
    justifyContent: 'space-between',
  },
  sectionTitle: {
    fontSize: 17,
    fontWeight: '700',
    color: colors.text,
    letterSpacing: -0.3,
  },
  sectionCount: {
    fontFamily: fonts.mono,
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  tabs: {
    paddingHorizontal: 20,
    paddingTop: 4,
    paddingBottom: 12,
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
  listItem: {
    paddingHorizontal: 12,
  },
});
