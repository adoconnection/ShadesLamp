import React from 'react';
import { View, Text, StyleSheet } from 'react-native';
import Slider from './Slider';
import Toggle from './Toggle';
import Picker from './Picker';
import { Param } from '../types/program';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface ParamControlProps {
  param: Param;
  accent: string;
  onChange: (value: number) => void;
  disabled?: boolean;
}

export default function ParamControl({ param, accent, onChange, disabled }: ParamControlProps) {
  return (
    <View style={[styles.container, disabled && { opacity: 0.4 }]}>
      <View style={styles.header}>
        <View>
          <Text style={styles.name}>{param.name}</Text>
          <Text style={styles.desc}>{param.desc}</Text>
        </View>
        <Text style={styles.typeLabel}>
          {param.type.toUpperCase()}
          {param.type !== 'bool' && param.type !== 'select'
            ? ` ${param.min}–${param.max}`
            : ''}
        </Text>
      </View>

      {param.type === 'int' && (
        <Slider
          value={param.value}
          min={param.min!}
          max={param.max!}
          step={1}
          color={accent}
          onChange={onChange}
          disabled={disabled}
        />
      )}

      {param.type === 'float' && (
        <Slider
          value={param.value}
          min={param.min!}
          max={param.max!}
          step={0.01}
          color={accent}
          onChange={onChange}
          formatValue={(v) => v.toFixed(2)}
          disabled={disabled}
        />
      )}

      {param.type === 'bool' && (
        <View style={styles.boolRow}>
          <Text style={styles.boolLabel}>
            {param.value ? 'enabled' : 'disabled'}
          </Text>
          <Toggle
            value={!!param.value}
            color={accent}
            onChange={(v) => onChange(v ? 1 : 0)}
            disabled={disabled}
          />
        </View>
      )}

      {param.type === 'select' && param.options && (
        <Picker
          options={param.options}
          value={param.value}
          color={accent}
          label={param.name}
          onChange={onChange}
          disabled={disabled}
        />
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    backgroundColor: 'rgba(255,255,255,0.04)',
    borderColor: 'rgba(255,255,255,0.06)',
    borderWidth: 0.5,
    borderRadius: 18,
    padding: 14,
    paddingHorizontal: 16,
  },
  header: {
    flexDirection: 'row',
    alignItems: 'flex-start',
    justifyContent: 'space-between',
    marginBottom: 10,
  },
  name: {
    fontSize: 14,
    fontWeight: '600',
    color: colors.text,
    letterSpacing: -0.1,
  },
  desc: {
    fontSize: 11,
    color: 'rgba(250,250,247,0.5)',
    marginTop: 2,
  },
  typeLabel: {
    fontFamily: fonts.mono,
    fontSize: 10,
    color: 'rgba(250,250,247,0.4)',
    letterSpacing: 1,
  },
  boolRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingTop: 4,
  },
  boolLabel: {
    fontFamily: fonts.mono,
    fontSize: 13,
    color: 'rgba(250,250,247,0.7)',
  },
});
