import { CMD, UPLOAD_CHUNK_SIZE, UPLOAD_TYPE } from './constants';
import { CommandQueue } from './protocol';
import { writeCommand, writeUploadChunk, writeActiveProgram, readActiveProgram } from './manager';

const queue = new CommandQueue();

function toLE32(value: number): Uint8Array {
  const buf = new ArrayBuffer(4);
  new DataView(buf).setInt32(0, value, true);
  return new Uint8Array(buf);
}

function floatToLE32(value: number): Uint8Array {
  const buf = new ArrayBuffer(4);
  new DataView(buf).setFloat32(0, value, true);
  return new Uint8Array(buf);
}

export async function getPrograms(): Promise<Array<{ id: number; name: string }>> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_PROGRAMS])));
}

export async function getParams(programId: number): Promise<any[]> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_PARAMS, programId])));
}

export async function setParam(programId: number, paramId: number, value: number, isFloat = false): Promise<any> {
  return queue.enqueue(() => {
    const valueBytes = isFloat ? floatToLE32(value) : toLE32(value);
    const payload = new Uint8Array([CMD.SET_PARAM, programId, paramId, ...valueBytes]);
    return writeCommand(payload);
  });
}

export async function getParamValues(programId: number): Promise<Record<string, number>> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_PARAM_VALUES, programId])));
}

export async function uploadWasm(
  data: Uint8Array,
  onProgress?: (phase: string, progress: number) => void,
): Promise<number> {
  // UPLOAD_START
  const startResult = await queue.enqueue(() => {
    const sizeBytes = toLE32(data.length);
    return writeCommand(new Uint8Array([CMD.UPLOAD_START, ...sizeBytes]));
  });

  // Write chunks
  const totalChunks = Math.ceil(data.length / UPLOAD_CHUNK_SIZE);
  for (let i = 0; i < totalChunks; i++) {
    const start = i * UPLOAD_CHUNK_SIZE;
    const end = Math.min(start + UPLOAD_CHUNK_SIZE, data.length);
    const chunk = data.slice(start, end);
    await writeUploadChunk(chunk);

    if (i > 0 && i % 10 === 0) {
      await new Promise(r => setTimeout(r, 20));
    }

    onProgress?.('uploading', (i + 1) / totalChunks);
  }

  // UPLOAD_FINISH
  const finishResult = await queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.UPLOAD_FINISH])),
  );

  return finishResult.id;
}

// Stream a firmware image to the device for OTA. Uses the same chunked upload
// pipeline as programs but with type=FIRMWARE; the device buffers it in PSRAM,
// flashes the inactive OTA slot, and reboots. The final response is
// { ok: true, ota: "flashing" } (the device reboots shortly after).
export async function uploadFirmware(
  data: Uint8Array,
  onProgress?: (progress: number) => void,
): Promise<any> {
  // UPLOAD_START: size(4 LE) + type=FIRMWARE
  await queue.enqueue(() => {
    const sizeBytes = toLE32(data.length);
    return writeCommand(new Uint8Array([CMD.UPLOAD_START, ...sizeBytes, UPLOAD_TYPE.FIRMWARE]));
  });

  const totalChunks = Math.ceil(data.length / UPLOAD_CHUNK_SIZE);
  for (let i = 0; i < totalChunks; i++) {
    const start = i * UPLOAD_CHUNK_SIZE;
    const end = Math.min(start + UPLOAD_CHUNK_SIZE, data.length);
    await writeUploadChunk(data.slice(start, end));
    if (i > 0 && i % 10 === 0) {
      await new Promise(r => setTimeout(r, 20));
    }
    onProgress?.((i + 1) / totalChunks);
  }

  // UPLOAD_FINISH -> { ok: true, ota: "flashing" }
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.UPLOAD_FINISH])));
}

// List a directory on the device flash. Returns
// [{ name, size, dir }, ...].
export async function listFiles(path: string): Promise<Array<{ name: string; size: number; dir: boolean }>> {
  return queue.enqueue(() => {
    const pathBytes = new TextEncoder().encode(path);
    return writeCommand(new Uint8Array([CMD.LIST_FILES, ...pathBytes]));
  });
}

// Write a text file to the device flash (parent dirs are created on the lamp).
// Files larger than one BLE packet are split: first chunk truncates+writes via
// WRITE_FILE, the rest is appended via APPEND_FILE. The whole sequence runs as
// one queued task so chunks can't interleave with other commands.
const FILE_CHUNK = 200; // conservative data bytes per BLE write
export async function writeFile(path: string, data: string): Promise<any> {
  const pathBytes = new TextEncoder().encode(path);
  const dataBytes = new TextEncoder().encode(data);
  return queue.enqueue(async () => {
    let result: any;
    let offset = 0;
    let first = true;
    do {
      const chunk = dataBytes.slice(offset, offset + FILE_CHUNK);
      const payload = new Uint8Array(2 + pathBytes.length + chunk.length);
      payload[0] = first ? CMD.WRITE_FILE : CMD.APPEND_FILE;
      payload[1] = pathBytes.length;
      payload.set(pathBytes, 2);
      payload.set(chunk, 2 + pathBytes.length);
      result = await writeCommand(payload);
      offset += FILE_CHUNK;
      first = false;
    } while (offset < dataBytes.length);
    return result;
  });
}

// Delete a file or (recursively) a directory on the device flash.
export async function deleteFile(path: string): Promise<any> {
  return queue.enqueue(() => {
    const pathBytes = new TextEncoder().encode(path);
    return writeCommand(new Uint8Array([CMD.DELETE_FILE, ...pathBytes]));
  });
}

// Read a file off the device flash (text files like /config.json or
// /programs/{id}/meta.json). Binary files (code.wasm) are not supported here —
// the response path decodes as UTF-8/JSON.
export async function getFile(path: string): Promise<any> {
  return queue.enqueue(() => {
    const pathBytes = new TextEncoder().encode(path);
    return writeCommand(new Uint8Array([CMD.GET_FILE, ...pathBytes]));
  });
}

// ── Playlist commands (firmware owns /playlists/{id}.json) ──────────────────
const te = new TextEncoder();

export async function plList(): Promise<any[]> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_LIST])));
}
export async function plGet(id: number): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_GET, id])));
}
export async function plCreate(name: string): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_CREATE, ...te.encode(name)])));
}
export async function plRename(id: number, name: string): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_RENAME, id, ...te.encode(name)])));
}
export async function plDelete(id: number): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_DELETE, id])));
}
export async function plSetRotation(id: number, mode: number, interval: number): Promise<any> {
  return queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.PL_SET_ROTATION, id, mode, interval & 0xff, (interval >> 8) & 0xff])));
}
export async function plAddPosition(id: number, posJson: string): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_ADD_POS, id, ...te.encode(posJson)])));
}
export async function plRemovePosition(id: number, index: number): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_DEL_POS, id, index])));
}
export async function plReorder(id: number, indices: number[]): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_REORDER, id, ...te.encode(JSON.stringify(indices))])));
}
// Edit the param snapshot of the position at `index` in playlist `id`.
export async function plSetPosParams(
  id: number,
  index: number,
  params: { id: number; value: number; f: boolean }[],
): Promise<any> {
  return queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.PL_SET_POS, id, index & 0xff, ...te.encode(JSON.stringify(params))])));
}

// Apply a position: switch to the program with these params using the same
// on-device crossfade as a normal program switch.
export async function applyPos(progId: number, params: { id: number; value: number; f: boolean }[]): Promise<any> {
  return queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.APPLY_POS, progId, ...te.encode(JSON.stringify(params))])));
}

// ── Lamp-driven playlist rotation ───────────────────────────────────────────
// The lamp owns rotation: PL_PLAY starts it (and jumps to `index`), PL_STOP
// halts it, PL_STATE reads back the live {playing, index}. The lamp advances
// positions itself per the playlist file's mode/interval and notifies the app
// via the PL_ADVANCE event.
export async function plPlay(id: number, index = 0): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_PLAY, id, index & 0xff])));
}
export async function plStop(): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_STOP])));
}
export async function plGetState(): Promise<{ playing: number; index: number }> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.PL_STATE])));
}

export async function deleteProgram(programId: number): Promise<any> {
  return queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.DELETE_PROGRAM, programId])),
  );
}

export async function setDeviceName(name: string): Promise<any> {
  return queue.enqueue(() => {
    const encoder = new TextEncoder();
    const nameBytes = encoder.encode(name);
    const payload = new Uint8Array([CMD.SET_NAME, ...nameBytes]);
    return writeCommand(payload);
  });
}

export async function getDeviceName(): Promise<string> {
  const result = await queue.enqueue(() =>
    writeCommand(new Uint8Array([CMD.GET_NAME])),
  );
  return result.name;
}

export async function getHwConfig(): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_HW_CONFIG])));
}

export async function reboot(): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.REBOOT])));
}

// Wipe all programs on the lamp (device name/hardware config are kept).
export async function clearStorage(): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.CLEAR_STORAGE])));
}

export async function getMeta(programId: number): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_META, programId])));
}

export async function setMeta(programId: number, meta: object): Promise<any> {
  // Meta JSON (now including i18n) easily exceeds a single BLE characteristic
  // write, so stream it through the chunked upload path. The firmware's
  // UPLOAD_START supports type=1 (META) with the target program id, then
  // UPLOAD_FINISH writes it via setProgramMeta.
  const data = new TextEncoder().encode(JSON.stringify(meta));

  // UPLOAD_START: size(4 LE) + type(1)=META + progId(1)
  await queue.enqueue(() => {
    const sizeBytes = toLE32(data.length);
    return writeCommand(new Uint8Array([CMD.UPLOAD_START, ...sizeBytes, 1, programId]));
  });

  // Stream the JSON bytes in chunks
  const totalChunks = Math.ceil(data.length / UPLOAD_CHUNK_SIZE);
  for (let i = 0; i < totalChunks; i++) {
    const start = i * UPLOAD_CHUNK_SIZE;
    const end = Math.min(start + UPLOAD_CHUNK_SIZE, data.length);
    await writeUploadChunk(data.slice(start, end));
    if (i > 0 && i % 10 === 0) {
      await new Promise(r => setTimeout(r, 20));
    }
  }

  // UPLOAD_FINISH -> { ok: true }
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.UPLOAD_FINISH])));
}

export async function getPower(): Promise<boolean> {
  const result = await queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_POWER])));
  return result.power;
}

export async function setPower(on: boolean): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.SET_POWER, on ? 1 : 0])));
}

export async function getStorage(): Promise<{ used: number; total: number; free: number }> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_STORAGE])));
}

export async function getOrder(): Promise<number[]> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_ORDER])));
}

export async function setOrder(ids: number[]): Promise<any> {
  const json = JSON.stringify(ids);
  const encoder = new TextEncoder();
  const payload = encoder.encode(json);
  const cmd = new Uint8Array(1 + payload.length);
  cmd[0] = CMD.SET_ORDER;
  cmd.set(payload, 1);
  return queue.enqueue(() => writeCommand(cmd));
}

export { writeActiveProgram as setActiveProgram, readActiveProgram as getActiveProgram };
