import React from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface ActionTileProps {
  icon: React.ReactNode;
  label: string;
  detail?: string | null;
  onPress: () => void;
}

export default function ActionTile({ icon, label, detail, onPress }: ActionTileProps) {
  return (
    <Pressable onPress={onPress} style={styles.tile}>
      <View style={styles.iconWrap}>{icon}</View>
      <Text style={styles.label}>{label}</Text>
      {detail && (
        <View style={styles.badge}>
          <Text style={styles.badgeText}>{detail}</Text>
        </View>
      )}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  tile: {
    flex: 1,
    backgroundColor: 'rgba(255,255,255,0.04)',
    borderColor: 'rgba(255,255,255,0.06)',
    borderWidth: 0.5,
    borderRadius: 18,
    padding: 14,
    paddingHorizontal: 10,
    gap: 8,
  },
  iconWrap: {
    opacity: 0.85,
  },
  label: {
    fontSize: 12,
    fontWeight: '500',
    color: colors.text,
    letterSpacing: -0.1,
  },
  badge: {
    position: 'absolute',
    top: 10,
    right: 10,
    backgroundColor: colors.accent,
    minWidth: 18,
    height: 18,
    borderRadius: 9,
    paddingHorizontal: 5,
    alignItems: 'center',
    justifyContent: 'center',
  },
  badgeText: {
    fontFamily: fonts.mono,
    fontSize: 10,
    fontWeight: '700',
    color: '#0A0A08',
  },
});
