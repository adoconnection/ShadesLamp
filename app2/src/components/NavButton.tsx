import React from 'react';
import { StyleSheet } from 'react-native';
import PressableScale from './PressableScale';

interface NavButtonProps {
  icon: React.ReactNode;
  onPress?: () => void;
  active?: boolean;
  accent?: string;
  accessibilityLabel?: string;
}

export default function NavButton({ icon, onPress, active, accent, accessibilityLabel }: NavButtonProps) {
  return (
    <PressableScale
      onPress={onPress}
      accessibilityRole="button"
      accessibilityLabel={accessibilityLabel}
      style={[
        styles.button,
        {
          backgroundColor: active ? (accent || '#FAFAF7') : 'rgba(0,0,0,0.35)',
        },
      ]}
    >
      {icon}
    </PressableScale>
  );
}

const styles = StyleSheet.create({
  button: {
    width: 40,
    height: 40,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
  },
});
