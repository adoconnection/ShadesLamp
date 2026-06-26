import { NativeModules, Platform } from 'react-native';
import { en, StringKey } from './en';
import { ru } from './ru';

export type Lang = 'en' | 'ru';

// Read the OS locale via core React Native modules (no extra native dependency).
function detectLocale(): string {
  try {
    if (Platform.OS === 'ios') {
      const s: any = NativeModules.SettingsManager?.settings;
      const apple = s?.AppleLocale || (Array.isArray(s?.AppleLanguages) ? s.AppleLanguages[0] : '');
      return apple || 'en';
    }
    return NativeModules.I18nManager?.localeIdentifier || 'en';
  } catch {
    return 'en';
  }
}

// Resolved once at startup. Default is English; Russian when the OS is Russian.
export const lang: Lang = detectLocale().toLowerCase().startsWith('ru') ? 'ru' : 'en';

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

// Pick a localized name/desc from a marketplace item's meta i18n, else the base value.
export function localized(
  item: { i18n?: { ru?: { name: string; desc: string } } },
  field: 'name' | 'desc',
  fallback: string,
): string {
  if (lang === 'ru' && item.i18n?.ru?.[field]) return item.i18n.ru[field];
  return fallback;
}
