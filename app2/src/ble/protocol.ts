import { FLAG_FINAL, FLAG_ERROR, RESPONSE_TIMEOUT_MS } from './constants';
import { Buffer } from 'buffer';

// Reassembles chunked BLE notifications into complete JSON responses
export class ChunkedResponseAssembler {
  private chunks = new Map<number, Uint8Array>();
  private resolve: ((json: any) => void) | null = null;
  private reject: ((err: Error) => void) | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;

  waitForResponse(): Promise<any> {
    this.chunks.clear();
    return new Promise((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
      this.timer = setTimeout(() => {
        this.reject?.(new Error('Response timeout'));
        this.cleanup();
      }, RESPONSE_TIMEOUT_MS);
    });
  }

  onNotification(data: Uint8Array) {
    if (data.length < 2) return;

    const seq = data[0];
    const flags = data[1];
    const payload = data.slice(2);

    this.chunks.set(seq, payload);

    if (flags & FLAG_ERROR) {
      const text = this.assembleText();
      this.reject?.(new Error(text || 'BLE error'));
      this.cleanup();
      return;
    }

    if (flags & FLAG_FINAL) {
      const text = this.assembleText();
      try {
        const json = JSON.parse(text);
        this.resolve?.(json);
      } catch {
        this.resolve?.(text);
      }
      this.cleanup();
    }
  }

  private assembleText(): string {
    const keys = Array.from(this.chunks.keys()).sort((a, b) => a - b);
    const parts: Uint8Array[] = [];
    for (const k of keys) {
      const chunk = this.chunks.get(k);
      if (chunk && chunk.length > 0) parts.push(chunk);
    }
    const combined = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let offset = 0;
    for (const p of parts) {
      combined.set(p, offset);
      offset += p.length;
    }
    return new TextDecoder().decode(combined);
  }

  private cleanup() {
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.resolve = null;
    this.reject = null;
    this.chunks.clear();
  }
}

// Serializes commands: only one in-flight at a time
export class CommandQueue {
  private queue: Array<() => Promise<void>> = [];
  private busy = false;

  async enqueue<T>(fn: () => Promise<T>): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      this.queue.push(async () => {
        try {
          resolve(await fn());
        } catch (e) {
          reject(e);
        }
      });
      this.processNext();
    });
  }

  private async processNext() {
    if (this.busy || this.queue.length === 0) return;
    this.busy = true;
    const fn = this.queue.shift()!;
    try {
      await fn();
    } finally {
      this.busy = false;
      this.processNext();
    }
  }
}
