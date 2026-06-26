import { Gradient, ProgramI18n } from './program';

export type RotationMode = 'off' | 'next' | 'random';

// A single saved parameter value within a favorite snapshot.
export interface FavoriteParam {
  id: number;
  value: number;
  isFloat: boolean;
}

// A favorite is a SNAPSHOT of a program plus its parameter values at save time,
// so the same program can have multiple saved states (e.g. "Aurora/1", "Aurora/2").
export interface FavoriteVariant {
  key: string;            // unique id for this saved state
  programId: number;      // device slot id at save time
  slug?: string;          // stable identity — preferred for re-resolving the program
  name: string;           // snapshot of the program name (display fallback when offline)
  desc?: string;
  cover?: Gradient;
  pulse?: string;
  i18n?: ProgramI18n;
  params: FavoriteParam[];
  createdAt: number;      // also used to order variants of the same program (/1, /2…)
}
