import { NativeModules, Platform } from 'react-native';
import { getLocales } from 'expo-localization';
import { ProgramI18n } from '../types/program';
import { en, StringKey } from './en';
import { ru } from './ru';

export type Lang = 'en' | 'ru';

// Resolve the OS language code (e.g. "ru", "en"). Layered so it works under the
// New Architecture / bridgeless, where legacy NativeModules access is unreliable:
//   1) expo-localization (the supported, arch-safe source)
//   2) Intl (pure JS, Hermes)
//   3) legacy NativeModules (last resort)
function detectLanguage(): string {
  try {
    const code = getLocales()?.[0]?.languageCode;
    if (code) return code;
  } catch {}

  try {
    const dtf: any = (Intl as any)?.DateTimeFormat;
    const tag = dtf ? new dtf().resolvedOptions().locale : '';
    if (tag) return tag;
  } catch {}

  try {
    if (Platform.OS === 'ios') {
      const s: any = NativeModules.SettingsManager?.settings;
      return s?.AppleLocale || (Array.isArray(s?.AppleLanguages) ? s.AppleLanguages[0] : '') || 'en';
    }
    return NativeModules.I18nManager?.localeIdentifier || 'en';
  } catch {}

  return 'en';
}

// Resolved once at startup. Default is English; Russian when the OS is Russian.
export const lang: Lang = detectLanguage().toLowerCase().startsWith('ru') ? 'ru' : 'en';

const dicts: Record<Lang, Record<string, string>> = { en, ru };

export function t(key: StringKey, vars?: Record<string, string | number>): string {
  let s = dicts[lang][key] ?? en[key] ?? String(key);
  if (vars) {
    for (const k of Object.keys(vars)) {
      s = s.split(`{${k}}`).join(String(vars[k]));
    }
  }
  return s;
}

// Program categories are data-driven; translate the known set, pass through the rest.
const categoryRu: Record<string, string> = {
  All: 'Все',
  Ambient: 'Эмбиент',
  Fire: 'Огонь',
  Nature: 'Природа',
  Particles: 'Частицы',
  Streams: 'Потоки',
  Effects: 'Эффекты',
};

export function tCategory(cat: string): string {
  return lang === 'ru' ? categoryRu[cat] ?? cat : cat;
}

// Pick a localized name/desc from a program/market item's meta i18n, else base.
export function localized(
  item: { i18n?: ProgramI18n },
  field: 'name' | 'desc',
  fallback: string,
): string {
  if (lang !== 'en') {
    const v = item.i18n?.[lang]?.[field];
    if (v) return v;
  }
  return fallback;
}

// Localized parameter name/desc from meta i18n.params[paramId], else fallback.
export function localizedParam(
  i18n: ProgramI18n | undefined,
  paramId: number,
  field: 'name' | 'desc',
  fallback: string,
): string {
  if (lang !== 'en') {
    const v = i18n?.[lang]?.params?.[String(paramId)]?.[field];
    if (v) return v;
  }
  return fallback;
}

// Localized select options for a parameter, else fallback.
export function localizedOptions(
  i18n: ProgramI18n | undefined,
  paramId: number,
  fallback?: string[],
): string[] | undefined {
  if (lang !== 'en') {
    const opts = i18n?.[lang]?.params?.[String(paramId)]?.options;
    if (opts && opts.length) return opts;
  }
  return fallback;
}
