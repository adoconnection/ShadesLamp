import React, { useState } from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';
import Toggle from './Toggle';
import { ChevronIcon } from './Icon';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface SettingsRowProps {
  label: string;
  detail?: string;
  mono?: boolean;
  chev?: boolean;
  toggle?: boolean;
  defaultOn?: boolean;
  onToggle?: (value: boolean) => void;
  danger?: boolean;
  last?: boolean;
  onPress?: () => void;
}

export default function SettingsRow({
  label,
  detail,
  mono,
  chev,
  toggle,
  defaultOn,
  onToggle,
  danger,
  last,
  onPress,
}: SettingsRowProps) {
  const [on, setOn] = useState(!!defaultOn);

  const handleToggle = (v: boolean) => {
    setOn(v);
    onToggle?.(v);
  };

  return (
    <Pressable
      onPress={onPress || (toggle ? () => handleToggle(!on) : undefined)}
      style={[styles.row, !last && styles.border]}
    >
      <Text style={[styles.label, danger && styles.danger]}>{label}</Text>
      {detail && (
        <Text style={[styles.detail, mono && styles.mono]}>{detail}</Text>
      )}
      {chev && <ChevronIcon color="rgba(250,250,247,0.4)" />}
      {toggle && <Toggle value={on} onChange={handleToggle} color={colors.text} />}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: 14,
    paddingHorizontal: 16,
    gap: 12,
  },
  border: {
    borderBottomWidth: 0.5,
    borderBottomColor: 'rgba(255,255,255,0.06)',
  },
  label: {
    flex: 1,
    fontSize: 14,
    color: colors.text,
  },
  danger: {
    color: colors.red,
    fontWeight: '600',
  },
  detail: {
    fontSize: 13,
    color: 'rgba(250,250,247,0.55)',
  },
  mono: {
    fontFamily: fonts.mono,
  },
});
