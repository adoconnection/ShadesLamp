import { FavoriteVariant } from '../types/favorites';
import { Program, Gradient } from '../types/program';
import { localized } from '../i18n';

export interface FavoriteDisplay {
  v: FavoriteVariant;
  name: string;          // resolved (localized) program name
  label: string;         // "name" or "name/n" when the program has multiple states
  cover: Gradient;
  pulse: string;
}

const DEFAULT_COVER: Gradient = { from: '#555555', to: '#999999', angle: 135 };
const DEFAULT_PULSE = '#888888';

// Resolve the live program for a variant — prefer the stable slug, fall back to
// the snapshot-time device id (which may have been reassigned after reinstall).
export function resolveProgram(v: FavoriteVariant, programs: Program[]): Program | undefined {
  if (v.slug) {
    const bySlug = programs.find((p) => p.slug && p.slug === v.slug);
    if (bySlug) return bySlug;
  }
  return programs.find((p) => p.id === v.programId);
}

// Build the sorted, numbered display list: sort by program name, then by save
// order; append "/n" only when a program has more than one saved state.
export function buildFavoriteList(variants: FavoriteVariant[], programs: Program[]): FavoriteDisplay[] {
  const list: FavoriteDisplay[] = variants.map((v) => {
    const p = resolveProgram(v, programs);
    return {
      v,
      name: p ? localized(p, 'name', p.name) : (v.name || `Program ${v.programId}`),
      label: '',
      cover: p?.cover || v.cover || DEFAULT_COVER,
      pulse: p?.pulse || v.pulse || DEFAULT_PULSE,
    };
  });

  list.sort(
    (a, b) =>
      a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) ||
      a.v.createdAt - b.v.createdAt,
  );

  const total: Record<string, number> = {};
  for (const e of list) total[e.name] = (total[e.name] || 0) + 1;
  const seen: Record<string, number> = {};
  for (const e of list) {
    seen[e.name] = (seen[e.name] || 0) + 1;
    e.label = total[e.name] > 1 ? `${e.name}/${seen[e.name]}` : e.name;
  }
  return list;
}
