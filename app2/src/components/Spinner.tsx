import React, { useEffect } from 'react';
import { StyleProp, ViewStyle } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  cancelAnimation,
  Easing,
} from 'react-native-reanimated';
import { colors } from '../theme/colors';

interface SpinnerProps {
  size?: number;
  color?: string;
  thickness?: number;
  /** Colour of the faint full ring behind the rotating arc. */
  trackColor?: string;
  style?: StyleProp<ViewStyle>;
}

/** A lightweight circular progress spinner (rotating arc). */
export default function Spinner({
  size = 18,
  color = colors.text,
  thickness = 2,
  trackColor = 'rgba(255,255,255,0.15)',
  style,
}: SpinnerProps) {
  const rot = useSharedValue(0);

  useEffect(() => {
    rot.value = withRepeat(
      withTiming(360, { duration: 850, easing: Easing.linear }),
      -1,
    );
    return () => cancelAnimation(rot);
  }, []);

  const animStyle = useAnimatedStyle(() => ({
    transform: [{ rotate: `${rot.value}deg` }],
  }));

  return (
    <Animated.View
      style={[
        {
          width: size,
          height: size,
          borderRadius: size / 2,
          borderWidth: thickness,
          borderColor: trackColor,
          borderTopColor: color,
        },
        animStyle,
        style,
      ]}
    />
  );
}
