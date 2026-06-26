export function formatDownloads(n: number): string {
  if (n >= 1000) return (n / 1000).toFixed(1) + 'k';
  return String(n);
}

export function padId(id: number): string {
  return String(id).padStart(2, '0');
}

// Returns true when semver `a` is strictly newer than `b` (e.g. "1.2.0" > "1.1.3").
export function isVersionNewer(a?: string, b?: string): boolean {
  if (!a || !b) return false;
  const pa = a.split('.').map((n) => parseInt(n, 10) || 0);
  const pb = b.split('.').map((n) => parseInt(n, 10) || 0);
  const len = Math.max(pa.length, pb.length);
  for (let i = 0; i < len; i++) {
    const x = pa[i] || 0;
    const y = pb[i] || 0;
    if (x > y) return true;
    if (x < y) return false;
  }
  return false;
}
