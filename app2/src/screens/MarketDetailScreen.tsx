import React, { useState } from 'react';
import { View, Text, ScrollView, Pressable, StyleSheet } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { LinearGradient } from 'expo-linear-gradient';
import { NativeStackScreenProps } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import { useMarketStore } from '../store/useMarketStore';
import { useBleStore } from '../store/useBleStore';
import { useProgramStore } from '../store/useProgramStore';
import { uploadWasm, setMeta, setActiveProgram, deleteProgram as bleDeleteProgram } from '../ble/commands';
import NavButton from '../components/NavButton';
import { BackIcon, DownloadIcon, CheckIcon, TrashIcon } from '../components/Icon';
import { gradientColors } from '../utils/color';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

type Props = NativeStackScreenProps<RootStackParamList, 'MarketDetail'>;

type Phase = 'idle' | 'downloading' | 'uploading' | 'verifying' | 'done' | 'error';

export default function MarketDetailScreen({ route, navigation }: Props) {
  const insets = useSafeAreaInsets();
  const { itemId } = route.params;
  const { catalog, installedSlugs, markInstalled, unmarkInstalled } = useMarketStore();
  const { connectionState } = useBleStore();
  const { activeId, setActiveId, addProgram, removeProgram } = useProgramStore();
  const item = catalog.find((i) => i.slug === itemId);

  const { programs } = useProgramStore();
  const installed = installedSlugs.includes(itemId);

  // Find the device program ID for this marketplace item by matching slug
  const existingProgram = programs.find((p) => p.slug === itemId);
  const [phase, setPhase] = useState<Phase>(installed ? 'done' : 'idle');
  const [progress, setProgress] = useState(installed ? 1 : 0);
  const [errorMsg, setErrorMsg] = useState('');
  const [wasmSize, setWasmSize] = useState(0);
  const [installedId, setInstalledId] = useState<number | null>(existingProgram?.id ?? null);

  if (!item) return null;

  const accent = item.pulse;
  const installing = phase === 'downloading' || phase === 'uploading' || phase === 'verifying';
  const connected = connectionState === 'connected';

  async function handleInstall() {
    if (!connected) {
      setErrorMsg('Connect to device first');
      setPhase('error');
      return;
    }

    try {
      // Phase 1: Download WASM from GitHub
      setPhase('downloading');
      setProgress(0);

      const response = await fetch(item!.wasmUrl);
      if (!response.ok) throw new Error(`Download failed: ${response.status}`);

      const arrayBuffer = await response.arrayBuffer();
      const wasmData = new Uint8Array(arrayBuffer);
      setWasmSize(wasmData.length);
      setProgress(1);

      // Phase 2: Upload to device via BLE
      setPhase('uploading');
      setProgress(0);

      const newId = await uploadWasm(wasmData, (_phase, p) => {
        setProgress(p);
      });

      // Phase 3: Set metadata on device
      setPhase('verifying');
      setProgress(0.5);

      await setMeta(newId, {
        name: item!.name,
        desc: item!.desc,
        author: item!.author,
        category: item!.category,
        cover: item!.cover,
        pulse: item!.pulse,
        tags: item!.tags,
        slug: item!.slug,
      });

      // Auto-activate the new program
      try {
        await setActiveProgram(newId);
        setActiveId(newId);
      } catch {}

      // Add to local program store
      addProgram({
        id: newId,
        name: item!.name,
        desc: item!.desc,
        author: item!.author,
        size: '',
        cover: item!.cover,
        coverSvg: item!.coverSvg,
        pulse: item!.pulse,
        category: item!.category,
        params: [],
        slug: item!.slug,
      });

      setInstalledId(newId);
      setProgress(1);
      setPhase('done');
      markInstalled(itemId);
    } catch (err: any) {
      setErrorMsg(err.message || 'Installation failed');
      setPhase('error');
    }
  }

  return (
    <ScrollView style={styles.container} bounces={false}>
      {/* Hero */}
      <View style={styles.heroSection}>
        <LinearGradient
          colors={gradientColors(item.cover)}
          start={{ x: 0, y: 0 }}
          end={{ x: 1, y: 1 }}
          style={StyleSheet.absoluteFill}
        />
        <LinearGradient
          colors={['transparent', colors.bg]}
          locations={[0.5, 1]}
          style={StyleSheet.absoluteFill}
        />
        <View style={[styles.nav, { paddingTop: insets.top + 8 }]}>
          <NavButton icon={<BackIcon />} onPress={() => navigation.goBack()} />
          <View style={{ width: 36 }} />
        </View>
        <View style={styles.heroInfo}>
          <Text style={styles.heroLabel}>{item.category} · by {item.author}{item.version ? ` · v${item.version}` : ''}</Text>
          <Text style={styles.heroTitle}>{item.name}</Text>
          <Text style={styles.heroDesc}>{item.desc}</Text>
        </View>
      </View>

      {/* Install / Progress */}
      <View style={styles.installWrap}>
        {phase === 'idle' && (
          <Pressable
            onPress={handleInstall}
            style={[styles.installBtn, { backgroundColor: accent, opacity: connected ? 1 : 0.5 }]}
          >
            <DownloadIcon color="#0A0A08" />
            <Text style={styles.installText}>
              {connected ? 'Install' : 'Connect to install'}
            </Text>
          </Pressable>
        )}

        {installing && (
          <View style={styles.progressCard}>
            <View style={styles.progressHeader}>
              <Text style={styles.progressLabel}>
                {phase === 'downloading' ? 'Downloading from GitHub'
                  : phase === 'uploading' ? 'Uploading to lamp'
                  : 'Setting metadata'}
              </Text>
              <Text style={styles.progressPercent}>{Math.round(progress * 100)}%</Text>
            </View>
            <View style={styles.progressTrack}>
              <View style={[styles.progressFill, { width: `${progress * 100}%`, backgroundColor: accent }]} />
            </View>
            <Text style={styles.progressHint}>
              {phase === 'downloading' && `GET ${item.wasmUrl.split('/').slice(-2).join('/')}`}
              {phase === 'uploading' && `BLE write 0xFF04 chunks · ${wasmSize} bytes`}
              {phase === 'verifying' && 'CMD_SET_META · writing metadata'}
            </Text>
          </View>
        )}

        {phase === 'done' && (
          <View style={{ gap: 10 }}>
            <View style={styles.doneCard}>
              <View style={styles.doneCircle}>
                <CheckIcon size={18} color="#0A0A08" />
              </View>
              <View style={{ flex: 1 }}>
                <Text style={styles.doneTitle}>
                  {installedId != null && activeId === installedId ? 'Installed & Running' : 'Installed'}
                </Text>
              </View>
              {installedId != null && activeId !== installedId && (
                <Pressable
                  onPress={async () => {
                    if (connected && installedId != null) {
                      try {
                        await setActiveProgram(installedId);
                        setActiveId(installedId);
                      } catch {}
                    }
                  }}
                  style={styles.playBtn}
                >
                  <Text style={styles.playText}>▶ Run</Text>
                </Pressable>
              )}
            </View>
            <Pressable
              onPress={async () => {
                if (connected && installedId != null) {
                  try {
                    await bleDeleteProgram(installedId);
                    removeProgram(installedId);
                    unmarkInstalled(itemId);
                    setInstalledId(null);
                    setPhase('idle');
                    setProgress(0);
                  } catch (e: any) {
                    setErrorMsg(e.message || 'Delete failed');
                    setPhase('error');
                  }
                }
              }}
              style={styles.deleteBtn}
            >
              <TrashIcon color={colors.red} />
              <Text style={styles.deleteText}>Remove from device</Text>
            </Pressable>
          </View>
        )}

        {phase === 'error' && (
          <View style={styles.errorCard}>
            <Text style={styles.errorText}>{errorMsg}</Text>
            <Pressable onPress={() => setPhase('idle')} style={styles.retryBtn}>
              <Text style={styles.retryText}>Retry</Text>
            </Pressable>
          </View>
        )}
      </View>

      {/* Tags */}
      {item.tags.length > 0 && (
        <View style={styles.tagsRow}>
          {item.tags.map((tag) => (
            <View key={tag} style={styles.tag}>
              <Text style={styles.tagText}>{tag}</Text>
            </View>
          ))}
        </View>
      )}

      {/* About */}
      <Text style={styles.sectionTitle}>About</Text>
      <Text style={styles.aboutText}>
        {item.desc}. Self-contained WASM module compiled for wasm32. Runs at 30 FPS on LED matrix using wasm3 interpreter.
      </Text>

      {/* Technical */}
      <Text style={styles.sectionTitle}>Technical</Text>
      <View style={styles.techCard}>
        <TechRow label="Module" value={`${item.slug}/main.wasm`} />
        <TechRow label="Source" value="github.com/adoconnection/ShadesLamp" />
        <TechRow label="Memory" value="1 page (64 KB)" last />
      </View>

      <View style={{ height: 40 }} />
    </ScrollView>
  );
}

function TechRow({ label, value, last }: { label: string; value: string; last?: boolean }) {
  return (
    <View style={[techStyles.row, !last && techStyles.border]}>
      <Text style={techStyles.label}>{label}</Text>
      <Text style={techStyles.value} numberOfLines={1}>{value}</Text>
    </View>
  );
}

const techStyles = StyleSheet.create({
  row: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingVertical: 12 },
  border: { borderBottomWidth: 0.5, borderBottomColor: 'rgba(255,255,255,0.06)' },
  label: { fontFamily: fonts.mono, fontSize: 12, color: 'rgba(250,250,247,0.5)' },
  value: { fontFamily: fonts.mono, fontSize: 12, color: colors.text, flex: 1, textAlign: 'right', marginLeft: 12 },
});

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  heroSection: { paddingBottom: 28, paddingHorizontal: 20 },
  nav: { flexDirection: 'row', justifyContent: 'space-between' },
  heroInfo: { marginTop: 50 },
  heroLabel: { fontFamily: fonts.mono, fontSize: 11, letterSpacing: 1, color: 'rgba(255,255,255,0.7)', textTransform: 'uppercase' },
  heroTitle: { fontSize: 36, fontWeight: '800', color: '#FAFAF7', letterSpacing: -1, lineHeight: 38, marginTop: 6 },
  heroDesc: { fontSize: 14, color: 'rgba(255,255,255,0.85)', marginTop: 6 },
  installWrap: { paddingHorizontal: 20, marginTop: -12 },
  installBtn: { borderRadius: 18, padding: 15, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8 },
  installText: { fontSize: 15, fontWeight: '700', color: '#0A0A08', letterSpacing: -0.2 },
  progressCard: { backgroundColor: 'rgba(255,255,255,0.04)', borderColor: 'rgba(255,255,255,0.06)', borderWidth: 0.5, borderRadius: 18, padding: 14 },
  progressHeader: { flexDirection: 'row', justifyContent: 'space-between', marginBottom: 8 },
  progressLabel: { fontSize: 13, fontWeight: '600', color: colors.text },
  progressPercent: { fontFamily: fonts.mono, color: 'rgba(250,250,247,0.6)' },
  progressTrack: { height: 6, borderRadius: 3, backgroundColor: 'rgba(255,255,255,0.08)', overflow: 'hidden' },
  progressFill: { height: '100%' },
  progressHint: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 10 },
  doneCard: { backgroundColor: 'rgba(52,211,153,0.1)', borderColor: 'rgba(52,211,153,0.25)', borderWidth: 0.5, borderRadius: 18, padding: 14, paddingHorizontal: 16, flexDirection: 'row', alignItems: 'center', gap: 12 },
  doneCircle: { width: 32, height: 32, borderRadius: 16, backgroundColor: colors.green, alignItems: 'center', justifyContent: 'center' },
  doneTitle: { fontSize: 14, fontWeight: '600', color: colors.green },
  playBtn: { backgroundColor: colors.green, borderRadius: 12, paddingVertical: 7, paddingHorizontal: 14 },
  playText: { fontSize: 13, fontWeight: '700', color: '#0A0A08' },
  errorCard: { backgroundColor: 'rgba(248,113,113,0.08)', borderColor: 'rgba(248,113,113,0.25)', borderWidth: 0.5, borderRadius: 18, padding: 14, alignItems: 'center', gap: 8 },
  errorText: { fontSize: 13, color: '#F87171', textAlign: 'center' },
  retryBtn: { paddingVertical: 6, paddingHorizontal: 16, borderRadius: 10, backgroundColor: 'rgba(255,255,255,0.08)' },
  retryText: { fontSize: 12, fontWeight: '600', color: colors.text },
  tagsRow: { flexDirection: 'row', flexWrap: 'wrap', paddingHorizontal: 20, paddingTop: 16, gap: 8 },
  tag: { backgroundColor: 'rgba(255,255,255,0.06)', borderRadius: 8, paddingVertical: 4, paddingHorizontal: 10 },
  tagText: { fontFamily: fonts.mono, fontSize: 11, color: 'rgba(250,250,247,0.6)' },
  sectionTitle: { paddingHorizontal: 20, paddingTop: 12, paddingBottom: 6, fontSize: 17, fontWeight: '700', color: colors.text, letterSpacing: -0.3 },
  aboutText: { paddingHorizontal: 20, paddingBottom: 16, fontSize: 14, color: 'rgba(250,250,247,0.7)', lineHeight: 22 },
  techCard: { marginHorizontal: 20, backgroundColor: 'rgba(255,255,255,0.04)', borderColor: 'rgba(255,255,255,0.06)', borderWidth: 0.5, borderRadius: 18, paddingHorizontal: 16, paddingVertical: 6 },
  deleteBtn: { backgroundColor: 'rgba(239,68,68,0.1)', borderRadius: 18, padding: 14, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8 },
  deleteText: { color: colors.red, fontSize: 14, fontWeight: '600' },
});
