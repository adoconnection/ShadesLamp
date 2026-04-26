import React from 'react';
import { View, Text, Pressable, StyleSheet } from 'react-native';

interface SegmentedProps {
  options: string[];
  value: number;
  color?: string;
  onChange: (index: number) => void;
}

export default function Segmented({ options, value, color = '#FAFAF7', onChange }: SegmentedProps) {
  return (
    <View style={styles.container}>
      {options.map((opt, i) => (
        <Pressable
          key={i}
          onPress={() => onChange(i)}
          style={[
            styles.button,
            {
              backgroundColor: value === i ? color : 'transparent',
            },
          ]}
        >
          <Text
            style={[
              styles.label,
              {
                color: value === i ? '#0A0A08' : 'rgba(250,250,247,0.65)',
              },
            ]}
          >
            {opt}
          </Text>
        </Pressable>
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 12,
    padding: 3,
    gap: 2,
  },
  button: {
    flex: 1,
    paddingVertical: 8,
    paddingHorizontal: 10,
    borderRadius: 10,
    alignItems: 'center',
  },
  label: {
    fontSize: 13,
    fontWeight: '600',
    letterSpacing: -0.1,
  },
});
