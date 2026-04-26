import React from 'react';
import { Pressable, StyleSheet } from 'react-native';

interface NavButtonProps {
  icon: React.ReactNode;
  onPress?: () => void;
  active?: boolean;
  accent?: string;
}

export default function NavButton({ icon, onPress, active, accent }: NavButtonProps) {
  return (
    <Pressable
      onPress={onPress}
      style={[
        styles.button,
        {
          backgroundColor: active ? (accent || '#FAFAF7') : 'rgba(0,0,0,0.35)',
        },
      ]}
    >
      {icon}
    </Pressable>
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
