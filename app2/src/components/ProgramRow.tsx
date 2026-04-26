import React from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
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
}

export default function ProgramRow({ program, active, isFavorite, onTap, onOpen }: ProgramRowProps) {
  return (
    <View style={[styles.container, active && styles.containerActive]}>
      <Pressable onPress={onTap} style={styles.coverWrap}>
        <Cover cover={program.cover} pulse={program.pulse} size={56} radius={12} animated={active} />
        {active && (
          <View style={styles.eqOverlay}>
            <View style={styles.eqBars}>
              {[0, 1, 2, 3].map((i) => (
                <View
                  key={i}
                  style={[styles.eqBar, { height: 6 + (i % 2) * 6 }]}
                />
              ))}
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
          {program.author} · {program.params.length} params · {program.size}
        </Text>
      </Pressable>

      <Pressable onPress={onOpen} style={styles.chevronBtn}>
        <ChevronIcon color="rgba(250,250,247,0.5)" />
      </Pressable>
    </View>
  );
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
