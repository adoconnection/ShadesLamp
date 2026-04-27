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
  cover: Gradient;
  pulse: string;
  category: string;
  params: Param[];
}
