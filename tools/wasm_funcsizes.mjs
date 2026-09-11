import { readFileSync } from 'fs';
const b = readFileSync(process.argv[2]); let p = 8;
const leb = () => { let r=0, s=0, x; do { x=b[p++]; r|=(x&127)<<s; s+=7; } while (x&128); return r>>>0; };
const sizes=[]; let mem=null;
while (p < b.length) { const id=b[p++]; const len=leb(); const end=p+len;
  if (id===10) { const n=leb(); for (let i=0;i<n;i++){ const sz=leb(); sizes.push(sz); p+=sz; } }
  else if (id===5) { const n=leb(); const flags=leb(); mem=leb(); }
  p=end; }
sizes.sort((a,b)=>b-a); console.log('memory pages', mem, '| funcs', sizes.length, '| largest', sizes.slice(0,6).join(', '));
