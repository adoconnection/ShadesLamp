import React from 'react';
import Svg, { Path, Circle, Rect } from 'react-native-svg';

interface IconProps {
  size?: number;
  color?: string;
}

export const BackIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M15 6l-6 6 6 6" stroke={color} strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const CloseIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M6 6l12 12M18 6l-12 12" stroke={color} strokeWidth={2} strokeLinecap="round" />
  </Svg>
);

export const MoreIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Circle cx={5} cy={12} r={1.6} fill={color} />
    <Circle cx={12} cy={12} r={1.6} fill={color} />
    <Circle cx={19} cy={12} r={1.6} fill={color} />
  </Svg>
);

export const SearchIcon = ({ size = 20, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Circle cx={11} cy={11} r={7} stroke={color} strokeWidth={2} />
    <Path d="M20 20l-3.5-3.5" stroke={color} strokeWidth={2} strokeLinecap="round" />
  </Svg>
);

export const LibraryIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Rect x={3} y={3} width={7} height={7} rx={1.5} stroke={color} strokeWidth={1.7} />
    <Rect x={14} y={3} width={7} height={7} rx={1.5} stroke={color} strokeWidth={1.7} />
    <Rect x={3} y={14} width={7} height={7} rx={1.5} stroke={color} strokeWidth={1.7} />
    <Rect x={14} y={14} width={7} height={7} rx={1.5} stroke={color} strokeWidth={1.7} />
  </Svg>
);

export const MarketIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M3 7l1.5 11a2 2 0 002 1.7h11a2 2 0 002-1.7L21 7M3 7h18M3 7l2-3h14l2 3M9 11v4M15 11v4" stroke={color} strokeWidth={1.7} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const SettingsIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Circle cx={12} cy={12} r={3} stroke={color} strokeWidth={1.7} />
    <Path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2" stroke={color} strokeWidth={1.7} strokeLinecap="round" />
  </Svg>
);

export const UploadIcon = ({ size = 20, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M12 16V4M6 10l6-6 6 6M4 20h16" stroke={color} strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const DownloadIcon = ({ size = 18, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M12 4v12M6 10l6 6 6-6M4 20h16" stroke={color} strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const TrashIcon = ({ size = 20, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M4 7h16M9 7V4h6v3M6 7l1 13a2 2 0 002 2h6a2 2 0 002-2l1-13M10 11v7M14 11v7" stroke={color} strokeWidth={1.7} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const StarFillIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill={color}>
    <Path d="M12 2.8l2.8 6.5 7 .7-5.3 4.7L18 21.4l-6-3.5-6 3.5 1.5-6.7L2.2 10l7-.7z" />
  </Svg>
);

export const StarOutlineIcon = ({ size = 22, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M12 2.8l2.8 6.5 7 .7-5.3 4.7L18 21.4l-6-3.5-6 3.5 1.5-6.7L2.2 10l7-.7z" stroke={color} strokeWidth={1.7} strokeLinejoin="round" />
  </Svg>
);

export const StarSmallIcon = ({ size = 14, color = '#FCD34D' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill={color}>
    <Path d="M12 2l3 7 7 .8-5 5L18 22l-6-3.5L6 22l1-7-5-5L9 9z" />
  </Svg>
);

export const CheckIcon = ({ size = 20, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M5 13l4 4 10-12" stroke={color} strokeWidth={2.5} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const BluetoothIcon = ({ size = 18, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M7 7l10 10-5 5V2l5 5L7 17" stroke={color} strokeWidth={1.8} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const ChevronIcon = ({ size = 16, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M9 6l6 6-6 6" stroke={color} strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const RefreshIcon = ({ size = 18, color = '#FAFAF7' }: IconProps) => (
  <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <Path d="M21 12a9 9 0 11-3-6.7L21 8M21 3v5h-5" stroke={color} strokeWidth={1.8} strokeLinecap="round" strokeLinejoin="round" />
  </Svg>
);

export const SignalIcon = ({ rssi, color = '#FAFAF7' }: { rssi: number; color?: string }) => {
  const bars = rssi > -55 ? 4 : rssi > -70 ? 3 : rssi > -85 ? 2 : 1;
  return (
    <Svg width={18} height={14} viewBox="0 0 18 14" fill="none">
      {[1, 2, 3, 4].map((i) => (
        <Rect
          key={i}
          x={(i - 1) * 4 + 1}
          y={14 - i * 3}
          width={2.5}
          height={i * 3}
          rx={0.5}
          fill={i <= bars ? color : 'rgba(255,255,255,0.18)'}
        />
      ))}
    </Svg>
  );
};
