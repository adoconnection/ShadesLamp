import React from 'react';
import { Text, StyleSheet } from 'react-native';
import { fonts } from '../theme/typography';

export default function SectionLabel({ children }: { children: string }) {
  return <Text style={styles.label}>{children}</Text>;
}

const styles = StyleSheet.create({
  label: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.45)',
    paddingHorizontal: 28,
    paddingTop: 4,
    paddingBottom: 8,
    textTransform: 'uppercase',
  },
});
