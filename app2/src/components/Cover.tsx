import React, { useEffect } from 'react';
import { View, StyleSheet, ViewStyle } from 'react-native';
import { LinearGradient } from 'expo-linear-gradient';
import { SvgXml } from 'react-native-svg';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  Easing,
} from 'react-native-reanimated';
import { Gradient } from '../types/program';
import { gradientColors } from '../utils/color';

interface CoverProps {
  cover: Gradient;
  coverSvg?: string;
  size?: number;
  radius?: number;
  animated?: boolean;
  pulse?: string;
  style?: ViewStyle;
}

export default function Cover({ cover, coverSvg, size = 56, radius = 14, animated = false, pulse, style }: CoverProps) {
  const colors = gradientColors(cover);
  const translateX = useSharedValue(0);
  const translateY = useSharedValue(0);

  useEffect(() => {
    if (animated) {
      translateX.value = withRepeat(
        withTiming(size * 0.3, { duration: 6000, easing: Easing.inOut(Easing.ease) }),
        -1,
        true,
      );
      translateY.value = withRepeat(
        withTiming(size * 0.2, { duration: 8000, easing: Easing.inOut(Easing.ease) }),
        -1,
        true,
      );
    }
  }, [animated]);

  const overlayStyle = useAnimatedStyle(() => ({
    transform: [
      { translateX: translateX.value },
      { translateY: translateY.value },
    ],
  }));

  return (
    <View
      style={[
        {
          width: size,
          height: size,
          borderRadius: radius,
          overflow: 'hidden',
        },
        animated && pulse && {
          shadowColor: pulse,
          shadowOffset: { width: 0, height: 8 },
          shadowOpacity: 0.4,
          shadowRadius: 16,
          elevation: 8,
        },
        style,
      ]}
    >
      {coverSvg ? (
        <SvgXml xml={coverSvg} width={size} height={size} style={StyleSheet.absoluteFill} />
      ) : (
        <LinearGradient
          colors={colors}
          start={{ x: 0, y: 0 }}
          end={{ x: 1, y: 1 }}
          style={StyleSheet.absoluteFill}
        />
      )}
      {animated && (
        <Animated.View
          style={[
            StyleSheet.absoluteFill,
            { opacity: 0.5 },
            overlayStyle,
          ]}
        >
          <LinearGradient
            colors={[
              (cover.via || cover.to) + 'aa',
              'transparent',
            ]}
            start={{ x: 0.3, y: 0.3 }}
            end={{ x: 0.7, y: 0.7 }}
            style={[StyleSheet.absoluteFill, { borderRadius: radius }]}
          />
        </Animated.View>
      )}
    </View>
  );
}
