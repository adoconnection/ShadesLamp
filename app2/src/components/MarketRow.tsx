import React from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
import Cover from './Cover';
import { CheckIcon, ChevronIcon, DownloadIcon } from './Icon';
import { MarketItem } from '../types/marketplace';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface MarketRowProps {
  item: MarketItem;
  installed: boolean;
  onPress: () => void;
}

export default function MarketRow({ item, installed, onPress }: MarketRowProps) {
  return (
    <Pressable onPress={onPress} style={styles.row}>
      <Cover cover={item.cover} coverSvg={item.coverSvg} pulse={item.pulse} size={56} radius={12} />
      <View style={styles.info}>
        <View style={styles.nameRow}>
          <Text style={styles.name} numberOfLines={1}>{item.name}</Text>
          {installed && <CheckIcon size={16} color={colors.green} />}
        </View>
        <View style={styles.metaRow}>
          <Text style={styles.metaText}>{item.category}</Text>
          <Text style={styles.sep}>·</Text>
          <Text style={styles.metaText}>{item.author}</Text>
        </View>
      </View>
      <View style={[styles.actionBtn, { backgroundColor: installed ? 'transparent' : item.pulse + '22' }]}>
        {installed
          ? <ChevronIcon color="rgba(250,250,247,0.5)" />
          : <DownloadIcon color={item.pulse} />
        }
      </View>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    backgroundColor: 'rgba(255,255,255,0.04)',
    borderColor: 'rgba(255,255,255,0.06)',
    borderWidth: 0.5,
    borderRadius: 16,
    padding: 10,
  },
  info: {
    flex: 1,
    minWidth: 0,
  },
  nameRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  name: {
    fontSize: 15,
    fontWeight: '600',
    color: colors.text,
    letterSpacing: -0.2,
    flexShrink: 1,
  },
  metaRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    marginTop: 2,
  },
  sep: {
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  metaText: {
    fontFamily: fonts.mono,
    fontSize: 12,
    color: 'rgba(250,250,247,0.5)',
  },
  actionBtn: {
    width: 36,
    height: 36,
    borderRadius: 18,
    alignItems: 'center',
    justifyContent: 'center',
    flexShrink: 0,
  },
});
