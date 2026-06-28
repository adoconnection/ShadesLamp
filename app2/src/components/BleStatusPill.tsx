import React, { useEffect } from 'react';
import { Text, StyleSheet } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withTiming,
  withSequence,
} from 'react-native-reanimated';
import { ConnectionState } from '../types/ble';
import PressableScale from './PressableScale';
import Spinner from './Spinner';
import { t } from '../i18n';
import { colors } from '../theme/colors';

interface BleStatusPillProps {
  state: ConnectionState;
  name?: string;
  /** True while background data (programs/playlists) is still loading. */
  syncing?: boolean;
  onPress: () => void;
}

export default function BleStatusPill({ state, name, syncing, onPress }: BleStatusPillProps) {
  const dotColor = state === 'connected' ? colors.green : state === 'connecting' ? colors.yellow : colors.gray;
  // Show a spinner (instead of the status dot) while connecting or while the
  // initial data sync after connect is still running.
  const busy = state === 'connecting' || (state === 'connected' && !!syncing);
  const spinnerColor = state === 'connecting' ? colors.yellow : colors.green;
  const label = state === 'connecting'
    ? t('statusConnecting')
    : state === 'connected'
      ? (syncing ? t('statusSyncing') : (name || t('statusConnected')))
      : t('statusNotConnected');

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
    <PressableScale onPress={onPress} style={styles.pill} accessibilityRole="button" accessibilityLabel={label}>
      {busy ? (
        <Spinner size={11} thickness={1.5} color={spinnerColor} />
      ) : (
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
      )}
      <Text style={styles.label}>{label}</Text>
    </PressableScale>
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
