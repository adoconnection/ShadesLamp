import { Program } from './program';
import { MarketItem } from './marketplace';

export type RootStackParamList = {
  Library: undefined;
  ProgramDetail: { programId: number };
  Favorites: undefined;
  Marketplace: undefined;
  MarketDetail: { itemId: string };
  BleConnect: undefined;
  DeviceSettings: undefined;
};
