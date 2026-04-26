import { CMD, UPLOAD_CHUNK_SIZE } from './constants';
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

export async function getMeta(programId: number): Promise<any> {
  return queue.enqueue(() => writeCommand(new Uint8Array([CMD.GET_META, programId])));
}

export async function setMeta(programId: number, meta: object): Promise<any> {
  return queue.enqueue(() => {
    const encoder = new TextEncoder();
    const jsonBytes = encoder.encode(JSON.stringify(meta));
    const payload = new Uint8Array([CMD.SET_META, programId, ...jsonBytes]);
    return writeCommand(payload);
  });
}

export { writeActiveProgram as setActiveProgram, readActiveProgram as getActiveProgram };
