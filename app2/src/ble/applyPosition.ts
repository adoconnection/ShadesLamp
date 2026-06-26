import { PlaylistPosition } from '../types/playlist';
import { useProgramStore } from '../store/useProgramStore';
import { useBleStore } from '../store/useBleStore';
import { setActiveProgram, setParam } from './commands';

// Apply a playlist position: activate its program (triggers the firmware
// crossfade) and push the saved parameter values. Re-resolves the program id by
// slug so it survives reinstalls / id reuse. No-op over BLE when disconnected
// (still updates the local active id so the UI reflects the choice).
export async function applyPosition(pos: PlaylistPosition): Promise<void> {
  const { programs, setActiveId, updateParamValue } = useProgramStore.getState();
  const prog = pos.slug
    ? programs.find((p) => p.slug === pos.slug)
    : programs.find((p) => p.id === pos.prog);
  const pid = prog ? prog.id : pos.prog;

  setActiveId(pid);
  if (useBleStore.getState().connectionState !== 'connected') return;

  try { await setActiveProgram(pid); } catch {}
  for (const par of pos.params) {
    try { await setParam(pid, par.id, par.value, par.f); } catch {}
    updateParamValue(pid, par.id, par.value);
  }
}
