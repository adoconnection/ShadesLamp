export function formatDownloads(n: number): string {
  if (n >= 1000) return (n / 1000).toFixed(1) + 'k';
  return String(n);
}

export function padId(id: number): string {
  return String(id).padStart(2, '0');
}
