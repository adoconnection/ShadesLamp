import { RotationMode } from './favorites';

export type { RotationMode };

// One saved position inside a playlist: a program + a snapshot of its params.
// Addressed by its array index within the playlist.
export interface PlaylistPosition {
  uid?: string;                // client-side stable key (for drag lists); not persisted
  guid?: string;               // stable program identity (primary re-resolution key)
  prog: number;                // device program id at save time (legacy fallback)
  slug?: string;               // program slug (secondary fallback / display)
  name?: string;               // snapshot program name (display fallback)
  params: { id: number; value: number; f: boolean }[]; // f = isFloat
}

// A playlist owned by the lamp at /playlists/{id}.json. Position order == array
// order. `mode` is the rotation mode; `interval` is seconds between positions.
export interface Playlist {
  id: number;
  name: string;
  mode: RotationMode;
  interval: number;
  positions: PlaylistPosition[];
}
