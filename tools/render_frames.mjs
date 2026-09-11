// Render frames of a program to a PNG strip: node render.mjs slug W H out.png [k=v ...] [--frames=6 --gap=15 --scale=8]
import { readFileSync, writeFileSync } from 'fs';
import { deflateSync } from 'zlib';

const args = process.argv.slice(2);
const opts = { frames: 6, gap: 15, scale: 8, warm: 150 };
const pos = [];
const params = {};
for (const a of args) {
  if (a.startsWith('--')) { const [k, v] = a.slice(2).split('='); opts[k] = +v; }
  else if (/^\d+=/.test(a)) { const [k, v] = a.split('='); params[+k] = +v; }
  else pos.push(a);
}
const [slug, Wa, Ha, out] = pos; const W = +Wa, H = +Ha;

function nz_hash2d(x,y){ let h=(Math.imul(x,374761393)+Math.imul(y,668265263)+1274126177)|0; h=Math.imul(h^(h>>>13),1274126177)|0; return (h>>>16)&0xFF; }
function nz_value(fx,fy){ const ix=fx>>8, iy=fy>>8; let tx=(fx&255)/255, ty=(fy&255)/255; tx=tx*tx*(3-2*tx); ty=ty*ty*(3-2*ty);
  const v00=nz_hash2d(ix,iy),v10=nz_hash2d(ix+1,iy),v01=nz_hash2d(ix,iy+1),v11=nz_hash2d(ix+1,iy+1); const top=v00+(v10-v00)*tx, bot=v01+(v11-v01)*tx; return top+(bot-top)*ty; }
function nz_fbm(fx,fy,oct){ oct=oct<1?1:(oct>4?4:oct); let sum=0,amp=1,norm=0,freq=1; for(let o=0;o<oct;o++){ sum+=nz_value(fx*freq,fy*freq)*amp; norm+=amp; amp*=0.5; freq*=2; } return sum/norm; }
let exp; const mem = () => new Uint8Array(exp.memory.buffer);
let px = new Uint8Array(W*H*3);
function aa_add(m,base,blen,w,h,x,y,r,g,b,c){ if(c<=0||x<0||y<0||x>=w||y>=h)return;const idx=(y*w+x)*3;if(idx+3>blen)return;const o=base+idx; let v;v=m[o]+((r*c+0.5)|0);m[o]=v>255?255:v;v=m[o+1]+((g*c+0.5)|0);m[o+1]=v>255?255:v; v=m[o+2]+((b*c+0.5)|0);m[o+2]=v>255?255:v; }
const env = {
  get_width:()=>W, get_height:()=>H,
  set_pixel:(x,y,r,g,b)=>{ if(x<0||y<0||x>=W||y>=H)return; const o=(y*W+x)*3; px[o]=r;px[o+1]=g;px[o+2]=b; },
  draw:()=>{}, get_param_i32:(id)=>params[id]??0, get_param_f32:(id)=>params[id]??0, set_param_i32:()=>{},
  m_sin:Math.sin,m_cos:Math.cos,m_sqrt:x=>x<=0?0:Math.sqrt(x),m_hypot:(x,y)=>Math.sqrt(x*x+y*y), m_atan2:Math.atan2,m_exp:Math.exp,m_pow:Math.pow,
  m_hsv:(h,s,v)=>{ v=v<0?0:(v>255?255:v); s=s<0?0:(s>255?255:s); let r,g,b; if(s===0){r=g=b=v;} else {const hh=h&255,region=(hh/43)|0,rem=(hh-region*43)*6;
      const p=(v*(255-s))>>8,q=(v*(255-((s*rem)>>8)))>>8,t=(v*(255-((s*(255-rem))>>8)))>>8;
      switch(region){case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break; case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;default:r=v;g=p;b=q;break;}} return (r<<16)|(g<<8)|b; },
  m_fade:(ptr,len,keep)=>{const m=mem();for(let i=0;i<len;i++)m[ptr+i]=(m[ptr+i]*keep)>>8;},
  m_fill:(ptr,n,rgb)=>{const m=mem();const r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;for(let i=0;i<n;i++){m[ptr+i*3]=r;m[ptr+i*3+1]=g;m[ptr+i*3+2]=b;}},
  m_noise_fill:(ptr,w,h,scale,ox,oy,oct)=>{const m=mem();if(ptr<0||w<=0||h<=0||ptr+w*h>m.length)return; for(let y=0;y<h;y++)for(let x=0;x<w;x++){let v=(nz_fbm(x*scale+ox,y*scale+oy,oct)+0.5)|0;m[ptr+y*w+x]=v<0?0:(v>255?255:v);}},
  m_blend:(ptr,w,h,fx,fy,rgb)=>{const m=mem();const blen=m.length-ptr;const r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255; const ix=Math.floor(fx),iy=Math.floor(fy),dx=fx-ix,dy=fy-iy;
    aa_add(m,ptr,blen,w,h,ix,iy,r,g,b,(1-dx)*(1-dy));aa_add(m,ptr,blen,w,h,ix+1,iy,r,g,b,dx*(1-dy)); aa_add(m,ptr,blen,w,h,ix,iy+1,r,g,b,(1-dx)*dy);aa_add(m,ptr,blen,w,h,ix+1,iy+1,r,g,b,dx*dy);},
  m_line:()=>{},
};
const bytes = readFileSync(`H:/ShadesLamp/programs/${slug}/main.wasm`);
const { instance } = await WebAssembly.instantiate(bytes, { env });
exp = instance.exports;
const meta = JSON.parse(new TextDecoder().decode(mem().subarray(exp.get_meta_ptr(), exp.get_meta_ptr()+exp.get_meta_len())));
for (const p of meta.params) if (params[p.id] === undefined) params[p.id] = p.default;
exp.init();
const fbPtr = exp.get_framebuffer ? exp.get_framebuffer() : -1;
const frame = () => fbPtr>=0 ? mem().subarray(fbPtr, fbPtr+W*H*3) : px;

let t = 0; for (let i=0;i<opts.warm;i++){ exp.update(t); t+=33; }
const S = opts.scale, F = opts.frames, G = opts.gap;
const OW = F*(W*S+4), OH = H*S;
const img = new Uint8Array(OW*OH*3);
for (let f=0; f<F; f++) {
  for (let i=0;i<G;i++){ exp.update(t); t+=33; }
  const fb = frame();
  for (let y=0;y<H;y++) for (let x=0;x<W;x++) {
    const o=(y*W+x)*3; const r=fb[o],g=fb[o+1],b=fb[o+2];
    // linear LED -> sRGB-ish for the eye
    const enc = v => Math.round(255*Math.pow(v/255, 1/2.2));
    const R=enc(r),Gc=enc(g),B=enc(b);
    for (let dy=0;dy<S;dy++) for (let dx=0;dx<S;dx++) {
      const X = f*(W*S+4) + x*S+dx, Y = (H-1-y)*S+dy; const oo=(Y*OW+X)*3; img[oo]=R; img[oo+1]=Gc; img[oo+2]=B;
    }
  }
}
// PNG encode
const crcTable = new Int32Array(256); for (let n=0;n<256;n++){ let c=n; for(let k=0;k<8;k++) c = c&1 ? 0xEDB88320 ^ (c>>>1) : c>>>1; crcTable[n]=c; }
const crc = (buf) => { let c=-1; for (const b of buf) c = crcTable[(c^b)&255] ^ (c>>>8); return (c^-1)>>>0; };
const chunk = (type, data) => { const len=Buffer.alloc(4); len.writeUInt32BE(data.length); const td=Buffer.concat([Buffer.from(type), data]); const c=Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
const raw = Buffer.alloc((OW*3+1)*OH); for (let y=0;y<OH;y++){ raw[y*(OW*3+1)]=0; Buffer.from(img.buffer, y*OW*3, OW*3).copy(raw, y*(OW*3+1)+1); }
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(OW,0); ihdr.writeUInt32BE(OH,4); ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
writeFileSync(out, Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
console.log('wrote', out, OW+'x'+OH);
