import { Program } from '../types/program';
import { BleDevice, DeviceInfo } from '../types/ble';

export const PROGRAMS_INSTALLED: Program[] = [
  {
    id: 0,
    name: 'RGB Cycle',
    desc: 'Перебор чистых R-G-B',
    author: 'built-in',
    size: '2.1 KB',
    cover: { from: '#FF3B5C', to: '#3B82FF', angle: 135 },
    pulse: '#FF3B5C',
    category: 'Effects',
    params: [
      { id: 0, name: 'Speed', type: 'int', min: 1, max: 200, default: 50, value: 80, desc: 'Скорость перебора' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 255, value: 200, desc: 'Яркость' },
    ],
  },
  {
    id: 1,
    name: 'Rainbow',
    desc: 'Плавная HSV-радуга',
    author: 'built-in',
    size: '3.4 KB',
    cover: { from: '#FF6B6B', to: '#FFD93D', via: '#6BCB77', angle: 95 },
    pulse: '#FFD93D',
    category: 'Ambient',
    params: [
      { id: 0, name: 'Speed', type: 'int', min: 1, max: 100, default: 30, value: 42, desc: 'Скорость вращения' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 128, value: 180, desc: 'Яркость' },
      { id: 2, name: 'Saturation', type: 'float', min: 0.0, max: 1.0, default: 1.0, value: 0.85, desc: 'Насыщенность' },
    ],
  },
  {
    id: 2,
    name: 'Random Blink',
    desc: 'Случайные вспышки',
    author: 'built-in',
    size: '2.8 KB',
    cover: { from: '#0F2027', to: '#2C5364', via: '#203A43', angle: 160 },
    pulse: '#7DF9FF',
    category: 'Effects',
    params: [
      { id: 0, name: 'Speed', type: 'int', min: 1, max: 100, default: 50, value: 60, desc: 'Частота вспышек' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 200, value: 200, desc: 'Яркость' },
      { id: 2, name: 'Fade', type: 'bool', default: 1, value: 1, desc: 'Плавное затухание' },
    ],
  },
  {
    id: 3,
    name: 'Aurora Drift',
    desc: 'Северное сияние, медленный дрейф',
    author: 'k.morov',
    size: '5.7 KB',
    cover: { from: '#0B3D2E', to: '#7B2CBF', via: '#06B6D4', angle: 200 },
    pulse: '#06B6D4',
    category: 'Ambient',
    params: [
      { id: 0, name: 'Speed', type: 'int', min: 1, max: 60, default: 12, value: 8, desc: 'Скорость дрейфа' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 160, value: 140, desc: 'Яркость' },
      { id: 2, name: 'Hue Shift', type: 'float', min: 0.0, max: 1.0, default: 0.5, value: 0.62, desc: 'Сдвиг оттенка' },
      { id: 3, name: 'Density', type: 'float', min: 0.0, max: 1.0, default: 0.7, value: 0.55, desc: 'Плотность лент' },
      { id: 4, name: 'Palette', type: 'select', options: ['Northern Lights', 'Solar Flare', 'Deep Ocean', 'Twilight', 'Forest Mist', 'Lavender Field', 'Sunrise Bay', 'Cosmic Dust', 'Emerald Pulse', 'Crimson Veil', 'Arctic Bloom', 'Neon Tokyo'], default: 0, value: 2, desc: 'Цветовая палитра' },
      { id: 5, name: 'Mode', type: 'select', options: ['Calm', 'Storm', 'Pulse'], default: 0, value: 0, desc: 'Режим движения' },
    ],
  },
  {
    id: 4,
    name: 'VU Meter',
    desc: 'Полоска уровня по микрофону телефона',
    author: 'audio.lab',
    size: '4.1 KB',
    cover: { from: '#FF006E', to: '#FB5607', via: '#FFBE0B', angle: 90 },
    pulse: '#FB5607',
    category: 'Visualizers',
    params: [
      { id: 0, name: 'Sensitivity', type: 'int', min: 1, max: 100, default: 50, value: 65, desc: 'Чувствительность' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 200, value: 220, desc: 'Яркость' },
      { id: 2, name: 'Peak Hold', type: 'bool', default: 1, value: 1, desc: 'Удержание пика' },
      { id: 3, name: 'Palette', type: 'select', options: ['Hot', 'Cold', 'Mono', 'Spectrum'], default: 0, value: 0, desc: 'Палитра' },
    ],
  },
  {
    id: 5,
    name: 'Snake',
    desc: 'Управление с телефона',
    author: 'pixel.games',
    size: '6.2 KB',
    cover: { from: '#10B981', to: '#064E3B', via: '#059669', angle: 145 },
    pulse: '#10B981',
    category: 'Games',
    params: [
      { id: 0, name: 'Speed', type: 'int', min: 1, max: 30, default: 8, value: 10, desc: 'Скорость' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 200, value: 200, desc: 'Яркость' },
      { id: 2, name: 'Walls', type: 'bool', default: 1, value: 0, desc: 'Стены' },
    ],
  },
  {
    id: 6,
    name: 'Candle',
    desc: 'Имитация свечи, тёплый дрейф',
    author: 'built-in',
    size: '2.3 KB',
    cover: { from: '#7F1D1D', to: '#FBBF24', via: '#DC2626', angle: 175 },
    pulse: '#FBBF24',
    category: 'Ambient',
    params: [
      { id: 0, name: 'Flicker', type: 'int', min: 1, max: 100, default: 50, value: 65, desc: 'Мерцание' },
      { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 180, value: 180, desc: 'Яркость' },
      { id: 2, name: 'Warmth', type: 'float', min: 0.0, max: 1.0, default: 0.7, value: 0.78, desc: 'Тёплость' },
    ],
  },
];

export const MARKETPLACE = [
  { id: 'm-plasma', name: 'Plasma Field', author: 'shaderkit', desc: 'Классическое плазменное поле, плавные переливы синусов', size: '7.4 KB', downloads: 12400, rating: 4.8, cover: { from: '#7C3AED', to: '#EC4899', via: '#3B82F6', angle: 120 }, pulse: '#EC4899', category: 'Ambient', featured: true, paramCount: 5 },
  { id: 'm-fire', name: 'Fireplace', author: 'cozy.lab', desc: 'Мерцающий камин — тёплые красно-оранжевые тона', size: '4.9 KB', downloads: 8650, rating: 4.9, cover: { from: '#7C2D12', to: '#FCD34D', via: '#EA580C', angle: 175 }, pulse: '#EA580C', category: 'Ambient', featured: true, paramCount: 4 },
  { id: 'm-matrix-rain', name: 'Matrix Rain', author: 'k.morov', desc: 'Падающие зелёные символы', size: '5.1 KB', downloads: 23100, rating: 4.7, cover: { from: '#022C22', to: '#10B981', via: '#064E3B', angle: 180 }, pulse: '#10B981', category: 'Effects', featured: true, paramCount: 3 },
  { id: 'm-tetris', name: 'Tetris', author: 'pixel.games', desc: 'Классика — управление с телефона', size: '8.8 KB', downloads: 5400, rating: 4.6, cover: { from: '#1E1B4B', to: '#F59E0B', via: '#7C3AED', angle: 90 }, pulse: '#F59E0B', category: 'Games', featured: false, paramCount: 4 },
  { id: 'm-spectrum', name: 'Spectrum Analyzer', author: 'audio.lab', desc: 'FFT-анализатор по микрофону, 16 полос', size: '6.3 KB', downloads: 9200, rating: 4.8, cover: { from: '#312E81', to: '#06B6D4', via: '#3B82F6', angle: 105 }, pulse: '#06B6D4', category: 'Visualizers', featured: false, paramCount: 6 },
  { id: 'm-snowfall', name: 'Snowfall', author: 'cozy.lab', desc: 'Падающий снег, мягкий синий', size: '3.8 KB', downloads: 4100, rating: 4.5, cover: { from: '#0C4A6E', to: '#E0F2FE', via: '#0EA5E9', angle: 200 }, pulse: '#E0F2FE', category: 'Ambient', featured: false, paramCount: 3 },
  { id: 'm-pong', name: 'Pong', author: 'pixel.games', desc: 'Двое играющих, тач-управление', size: '5.5 KB', downloads: 2300, rating: 4.3, cover: { from: '#0F172A', to: '#22D3EE', via: '#1E293B', angle: 120 }, pulse: '#22D3EE', category: 'Games', featured: false, paramCount: 3 },
  { id: 'm-clock', name: 'Pixel Clock', author: 'shaderkit', desc: 'Цифровые часы 16×32, разные шрифты', size: '9.1 KB', downloads: 18700, rating: 4.9, cover: { from: '#0A0A0A', to: '#F472B6', via: '#1F2937', angle: 145 }, pulse: '#F472B6', category: 'Effects', featured: true, paramCount: 4 },
];

export const CATEGORIES = ['All', 'Effects', 'Ambient', 'Games', 'Visualizers'] as const;

export const DEVICE_INFO: DeviceInfo = {
  name: 'Shades Lamp',
  serial: 'SL-A37F-2204',
  mac: 'C8:C9:A3:7F:22:04',
  firmware: 'v0.4.1',
  matrix: '16 × 32',
  storage: { used: 31, total: 256 },
  uptime: '4d 12h',
  rssi: -52,
};

export const NEARBY_DEVICES: BleDevice[] = [
  { name: 'Shades Lamp', mac: 'C8:C9:A3:7F:22:04', rssi: -52, paired: true },
  { name: 'Shades Lamp', mac: 'A1:B2:C3:D4:E5:F6', rssi: -71, paired: false },
  { name: 'WasmLED-dev', mac: '4F:88:11:22:33:44', rssi: -84, paired: false },
];
