import { Gradient } from './program';

export interface MarketItem {
  slug: string;
  name: string;
  author: string;
  desc: string;
  category: string;
  cover: Gradient;
  pulse: string;
  tags: string[];
  wasmUrl: string;
  metaUrl: string;
  i18n?: { ru?: { name: string; desc: string } };
}
