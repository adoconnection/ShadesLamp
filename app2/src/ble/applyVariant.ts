import { FavoriteVariant } from '../types/favorites';
import { useProgramStore } from '../store/useProgramStore';
import { useBleStore } from '../store/useBleStore';
import { resolveProgram } from '../utils/favorites';
import { setActiveProgram, setParam } from './commands';

// Apply a saved favorite: activate its program and push the snapshot's parameter
// values to the lamp. Re-resolves the program by slug so it works even if the
// device reassigned ids after a reinstall. No-op over BLE when disconnected
// (still updates the local active id so the UI reflects the choice).
export async function applyVariant(v: FavoriteVariant): Promise<void> {
  const { programs, setActiveId, updateParamValue } = useProgramStore.getState();
  const prog = resolveProgram(v, programs);
  const pid = prog ? prog.id : v.programId;

  setActiveId(pid);

  if (useBleStore.getState().connectionState !== 'connected') return;

  try {
    await setActiveProgram(pid);
  } catch {
    // ignore; param writes below will also no-op on failure
  }

  for (const par of v.params) {
    try {
      await setParam(pid, par.id, par.value, par.isFloat);
    } catch {
      // continue applying the rest
    }
    updateParamValue(pid, par.id, par.value);
  }
}
