import React from 'react';
import { View, Text, StyleSheet } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  runOnJS,
} from 'react-native-reanimated';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface SliderProps {
  value: number;
  min: number;
  max: number;
  step?: number;
  color?: string;
  onChange: (v: number) => void;
  formatValue?: (v: number) => string;
  disabled?: boolean;
}

export default function Slider({
  value,
  min,
  max,
  step = 1,
  color = colors.text,
  onChange,
  formatValue,
  disabled,
}: SliderProps) {
  const trackWidth = useSharedValue(0);
  const ratio = (value - min) / (max - min);

  const clampValue = (clientX: number, width: number) => {
    'worklet';
    const r = Math.max(0, Math.min(1, clientX / width));
    let v = min + r * (max - min);
    v = Math.round(v / step) * step;
    v = Math.max(min, Math.min(max, v));
    runOnJS(onChange)(v);
  };

  const pan = Gesture.Pan()
    .onStart((e) => {
      clampValue(e.x, trackWidth.value);
    })
    .onUpdate((e) => {
      clampValue(e.x, trackWidth.value);
    });

  const tap = Gesture.Tap()
    .onEnd((e) => {
      clampValue(e.x, trackWidth.value);
    });

  const composed = Gesture.Race(pan, tap);

  const fillStyle = useAnimatedStyle(() => ({
    width: `${ratio * 100}%`,
  }));

  return (
    <GestureDetector gesture={composed}>
      <Animated.View
        pointerEvents={disabled ? 'none' : 'auto'}
        style={styles.track}
        onLayout={(e) => {
          trackWidth.value = e.nativeEvent.layout.width;
        }}
      >
        <Animated.View style={[styles.fill, { backgroundColor: color }, fillStyle]} />
        <View style={styles.labels}>
          <Text
            style={[
              styles.value,
              { color: ratio > 0.15 ? '#0A0A08' : 'rgba(250,250,247,0.6)' },
            ]}
          >
            {formatValue ? formatValue(value) : value}
          </Text>
          <Text style={styles.max}>{max}</Text>
        </View>
      </Animated.View>
    </GestureDetector>
  );
}

const styles = StyleSheet.create({
  track: {
    height: 44,
    borderRadius: 22,
    backgroundColor: 'rgba(255,255,255,0.06)',
    overflow: 'hidden',
    justifyContent: 'center',
  },
  fill: {
    position: 'absolute',
    top: 0,
    bottom: 0,
    left: 0,
    borderRadius: 22,
  },
  labels: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 16,
  },
  value: {
    fontFamily: fonts.mono,
    fontSize: 13,
    fontWeight: '500',
  },
  max: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: 'rgba(250,250,247,0.35)',
  },
});
