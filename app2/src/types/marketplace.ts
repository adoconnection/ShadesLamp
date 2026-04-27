import { Gradient } from './program';

export interface MarketItem {
  slug: string;
  name: string;
  author: string;
  desc: string;
  category: string;
  version?: string;
  cover: Gradient;
  coverSvg?: string;
  pulse: string;
  tags: string[];
  rating?: number;
  downloads?: number;
  wasmUrl: string;
  metaUrl: string;
  i18n?: { ru?: { name: string; desc: string } };
}
