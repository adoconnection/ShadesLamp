import React, { useEffect } from 'react';
import { Pressable, Text, View, StyleSheet } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  withSequence,
} from 'react-native-reanimated';
import { ConnectionState } from '../types/ble';
import { colors } from '../theme/colors';

interface BleStatusPillProps {
  state: ConnectionState;
  name?: string;
  onPress: () => void;
}

export default function BleStatusPill({ state, name, onPress }: BleStatusPillProps) {
  const dotColor = state === 'connected' ? colors.green : state === 'connecting' ? colors.yellow : colors.gray;
  const label = state === 'connected' ? (name || 'Connected') : state === 'connecting' ? 'Connecting…' : 'Not connected';

  const pulseOpacity = useSharedValue(1);

  useEffect(() => {
    if (state === 'connecting') {
      pulseOpacity.value = withRepeat(
        withSequence(
          withTiming(0.3, { duration: 600 }),
          withTiming(1, { duration: 600 }),
        ),
        -1,
      );
    } else {
      pulseOpacity.value = 1;
    }
  }, [state]);

  const dotAnimStyle = useAnimatedStyle(() => ({
    opacity: pulseOpacity.value,
  }));

  return (
    <Pressable onPress={onPress} style={styles.pill}>
      <Animated.View
        style={[
          styles.dot,
          { backgroundColor: dotColor },
          state === 'connected' && {
            shadowColor: dotColor,
            shadowOffset: { width: 0, height: 0 },
            shadowOpacity: 0.8,
            shadowRadius: 4,
          },
          dotAnimStyle,
        ]}
      />
      <Text style={styles.label}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  pill: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingVertical: 7,
    paddingLeft: 10,
    paddingRight: 12,
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 999,
  },
  dot: {
    width: 7,
    height: 7,
    borderRadius: 3.5,
  },
  label: {
    color: colors.text,
    fontSize: 12,
    fontWeight: '500',
    letterSpacing: -0.1,
  },
});
