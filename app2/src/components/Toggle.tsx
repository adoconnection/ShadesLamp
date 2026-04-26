import React from 'react';
import { Pressable, StyleSheet } from 'react-native';
import Animated, {
  useAnimatedStyle,
  withTiming,
  Easing,
} from 'react-native-reanimated';

interface ToggleProps {
  value: boolean;
  color?: string;
  onChange: (v: boolean) => void;
}

export default function Toggle({ value, color = '#FAFAF7', onChange }: ToggleProps) {
  const thumbStyle = useAnimatedStyle(() => ({
    left: withTiming(value ? 23 : 3, {
      duration: 200,
      easing: Easing.bezier(0.4, 1.4, 0.6, 1),
    }),
  }));

  const trackStyle = useAnimatedStyle(() => ({
    backgroundColor: withTiming(value ? color : 'rgba(255,255,255,0.1)', {
      duration: 200,
    }),
  }));

  return (
    <Pressable onPress={() => onChange(!value)}>
      <Animated.View style={[styles.track, trackStyle]}>
        <Animated.View
          style={[
            styles.thumb,
            { backgroundColor: value ? '#0A0A08' : '#FAFAF7' },
            thumbStyle,
          ]}
        />
      </Animated.View>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  track: {
    width: 52,
    height: 32,
    borderRadius: 16,
  },
  thumb: {
    position: 'absolute',
    top: 3,
    width: 26,
    height: 26,
    borderRadius: 13,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 1 },
    shadowOpacity: 0.3,
    shadowRadius: 3,
    elevation: 3,
  },
});
