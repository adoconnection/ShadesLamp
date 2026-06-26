export interface Gradient {
  from: string;
  to: string;
  via?: string;
  angle: number;
}

export interface Param {
  id: number;
  name: string;
  type: 'int' | 'float' | 'bool' | 'select';
  min?: number;
  max?: number;
  default: number;
  value: number;
  desc: string;
  options?: string[];
}

// Per-language overrides from a program's meta i18n. `params` is keyed by
// the param id (as a string) and overrides the English name/desc/options.
export interface ParamI18n {
  name?: string;
  desc?: string;
  options?: string[];
}

export interface LangI18n {
  name?: string;
  desc?: string;
  params?: { [paramId: string]: ParamI18n };
}

export interface ProgramI18n {
  [lang: string]: LangI18n;
}

export interface ProgramListItem {
  id: number;
  name: string;
  guid?: string;
  version?: string;
}

export interface Program {
  id: number;
  name: string;
  desc: string;
  author: string;
  size: string;
  version?: string;
  cover: Gradient;
  coverSvg?: string;
  pulse: string;
  category: string;
  params: Param[];
  slug?: string;
  i18n?: ProgramI18n;
}
