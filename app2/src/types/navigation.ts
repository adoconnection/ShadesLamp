import { Program } from './program';
import { MarketItem } from './marketplace';

export type RootStackParamList = {
  Library: undefined;
  ProgramDetail: { programId: number };
  Favorites: undefined;
  PlaylistDetail: { playlistId: number };
  PositionEdit: { playlistId: number; index: number };
  Marketplace: undefined;
  MarketDetail: { itemId: string };
  BleConnect: undefined;
  DeviceSettings: undefined;
};
