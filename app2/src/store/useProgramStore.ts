import { create } from 'zustand';
import { Program } from '../types/program';

interface ProgramState {
  programs: Program[];
  activeId: number;
  setPrograms: (programs: Program[]) => void;
  setActiveId: (id: number) => void;
  updateParamValue: (programId: number, paramId: number, value: number) => void;
  setProgramParams: (programId: number, params: Program['params']) => void;
  addProgram: (program: Program) => void;
  removeProgram: (programId: number) => void;
  reorderPrograms: (programs: Program[]) => void;
}

export const useProgramStore = create<ProgramState>((set) => ({
  programs: [],
  activeId: -1,
  setPrograms: (programs) => set({ programs }),
  setActiveId: (activeId) => set({ activeId }),
  updateParamValue: (programId, paramId, value) =>
    set((state) => ({
      programs: state.programs.map((p) =>
        p.id === programId
          ? {
              ...p,
              params: p.params.map((param) =>
                param.id === paramId ? { ...param, value } : param,
              ),
            }
          : p,
      ),
    })),
  setProgramParams: (programId, params) =>
    set((state) => ({
      programs: state.programs.map((p) =>
        p.id === programId ? { ...p, params } : p,
      ),
    })),
  addProgram: (program) =>
    set((state) => ({ programs: [...state.programs, program] })),
  removeProgram: (programId) =>
    set((state) => ({
      programs: state.programs.filter((p) => p.id !== programId),
      activeId: state.activeId === programId ? 0 : state.activeId,
    })),
  reorderPrograms: (programs) => set({ programs }),
}));
