import React, { useEffect } from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withSequence,
  withTiming,
  Easing,
} from 'react-native-reanimated';
import Cover from './Cover';
import { ChevronIcon, StarSmallIcon } from './Icon';
import { Program } from '../types/program';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface ProgramRowProps {
  program: Program;
  active: boolean;
  isFavorite?: boolean;
  onTap: () => void;
  onOpen: () => void;
  onLongPress?: () => void;
  isDragging?: boolean;
}

export default function ProgramRow({ program, active, isFavorite, onTap, onOpen, onLongPress, isDragging }: ProgramRowProps) {
  return (
    <Pressable
      onLongPress={onLongPress}
      delayLongPress={200}
      style={[styles.container, active && styles.containerActive, isDragging && styles.containerDragging]}
    >
      <Pressable onPress={onTap} style={styles.coverWrap}>
        <Cover cover={program.cover} coverSvg={program.coverSvg} pulse={program.pulse} size={56} radius={12} animated={active} />
        {active && (
          <View style={styles.eqOverlay}>
            <View style={styles.eqBars}>
              <EqBar duration={500} minH={4} maxH={14} delay={0} />
              <EqBar duration={700} minH={4} maxH={16} delay={100} />
              <EqBar duration={600} minH={4} maxH={12} delay={200} />
              <EqBar duration={900} minH={4} maxH={14} delay={50} />
            </View>
          </View>
        )}
      </Pressable>

      <Pressable onPress={onTap} style={styles.info}>
        <View style={styles.nameRow}>
          <Text
            style={[styles.name, active && { color: program.pulse }]}
            numberOfLines={1}
          >
            {program.name}
          </Text>
          {isFavorite && <StarSmallIcon size={12} />}
        </View>
        <Text style={styles.meta} numberOfLines={1}>
          {program.author}{program.category ? ` · ${program.category}` : ''}
        </Text>
      </Pressable>

      <Pressable onPress={onOpen} style={styles.chevronBtn}>
        <ChevronIcon color="rgba(250,250,247,0.5)" />
      </Pressable>
    </Pressable>
  );
}

function EqBar({ duration, minH, maxH, delay }: { duration: number; minH: number; maxH: number; delay: number }) {
  const height = useSharedValue(minH);

  useEffect(() => {
    height.value = withRepeat(
      withSequence(
        withTiming(maxH, { duration, easing: Easing.inOut(Easing.ease) }),
        withTiming(minH, { duration, easing: Easing.inOut(Easing.ease) }),
      ),
      -1,
    );
  }, []);

  const style = useAnimatedStyle(() => ({
    height: height.value,
  }));

  return <Animated.View style={[styles.eqBar, style]} />;
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    padding: 8,
    borderRadius: 14,
  },
  containerActive: {
    backgroundColor: 'rgba(255,255,255,0.04)',
  },
  containerDragging: {
    backgroundColor: 'rgba(255,255,255,0.08)',
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.3,
    shadowRadius: 12,
    elevation: 8,
  },
  coverWrap: {
    position: 'relative',
  },
  eqOverlay: {
    ...StyleSheet.absoluteFillObject,
    borderRadius: 12,
    backgroundColor: 'rgba(0,0,0,0.35)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  eqBars: {
    flexDirection: 'row',
    gap: 2.5,
    alignItems: 'flex-end',
    height: 16,
  },
  eqBar: {
    width: 3,
    backgroundColor: colors.text,
    borderRadius: 1.5,
  },
  info: {
    flex: 1,
    minWidth: 0,
  },
  nameRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
  name: {
    fontSize: 15,
    fontWeight: '600',
    color: colors.text,
    letterSpacing: -0.2,
    flexShrink: 1,
  },
  meta: {
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
    marginTop: 2,
  },
  chevronBtn: {
    width: 36,
    height: 36,
    borderRadius: 18,
    backgroundColor: 'rgba(255,255,255,0.06)',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
