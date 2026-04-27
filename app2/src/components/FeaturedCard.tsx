import React from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
import { LinearGradient } from 'expo-linear-gradient';
import { SvgXml } from 'react-native-svg';
import { MarketItem } from '../types/marketplace';
import { gradientColors } from '../utils/color';
import { CheckIcon } from './Icon';
import { fonts } from '../theme/typography';

interface FeaturedCardProps {
  item: MarketItem;
  installed: boolean;
  onPress: () => void;
}

export default function FeaturedCard({ item, installed, onPress }: FeaturedCardProps) {
  const gColors = gradientColors(item.cover);

  return (
    <Pressable onPress={onPress} style={styles.card}>
      {item.coverSvg ? (
        <SvgXml xml={item.coverSvg} width="100%" height="100%" preserveAspectRatio="xMidYMid slice" style={StyleSheet.absoluteFill} />
      ) : (
        <LinearGradient colors={gColors} start={{ x: 0, y: 0 }} end={{ x: 1, y: 1 }} style={StyleSheet.absoluteFill} />
      )}
      <LinearGradient colors={['transparent', 'rgba(0,0,0,0.6)']} locations={[0.5, 1]} style={StyleSheet.absoluteFill} />

      <View style={styles.top}>
        <View style={styles.categoryBadge}>
          <Text style={styles.categoryText}>{item.category.toUpperCase()}</Text>
        </View>
        {installed && (
          <View style={styles.checkBadge}>
            <CheckIcon size={14} color="#34D399" />
          </View>
        )}
      </View>

      <View style={styles.bottom}>
        <Text style={styles.name}>{item.name}</Text>
        <Text style={styles.author}>
          {item.author}
          {item.rating != null ? ` · ★ ${item.rating.toFixed(1)}` : ''}
          {item.downloads != null ? ` · ${item.downloads}` : ''}
        </Text>
      </View>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  card: {
    width: 220,
    minHeight: 260,
    borderRadius: 22,
    overflow: 'hidden',
  },
  top: {
    position: 'absolute',
    top: 14,
    left: 14,
    right: 14,
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  categoryBadge: {
    backgroundColor: 'rgba(0,0,0,0.35)',
    borderRadius: 999,
    paddingVertical: 5,
    paddingHorizontal: 10,
  },
  categoryText: {
    fontFamily: fonts.mono,
    fontSize: 10,
    letterSpacing: 1,
    color: '#FAFAF7',
  },
  checkBadge: {
    width: 26,
    height: 26,
    borderRadius: 13,
    backgroundColor: 'rgba(0,0,0,0.35)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  bottom: {
    position: 'absolute',
    left: 16,
    right: 16,
    bottom: 14,
  },
  name: {
    fontSize: 22,
    fontWeight: '800',
    color: '#FAFAF7',
    letterSpacing: -0.5,
    lineHeight: 24,
  },
  author: {
    fontSize: 12,
    color: 'rgba(255,255,255,0.85)',
    marginTop: 4,
  },
});
