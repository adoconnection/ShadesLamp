import { readFileSync } from 'fs';

const W = 40, H = 20;

function makeHost(getMem, params) {
  let memU8 = null;
  const mem = () => (memU8 ??= new Uint8Array(getMem().buffer));
  function host_m_hsv(h,s,v){
    v=v<0?0:(v>255?255:v); s=s<0?0:(s>255?255:s);
    let r,g,b;
    if(s===0){r=g=b=v;}
    else{const hh=h&0xFF,region=(hh/43)|0,rem=(hh-region*43)*6;
      const p=(v*(255-s))>>8,q=(v*(255-((s*rem)>>8)))>>8,t=(v*(255-((s*(255-rem))>>8)))>>8;
      switch(region){case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break;
        case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;default:r=v;g=p;b=q;break;}}
    return ((r<<16)|(g<<8)|b);
  }
  function aa_add(m,base,blen,w,h,x,y,r,g,b,c){
    if(c<=0||x<0||y<0||x>=w||y>=h)return;const idx=(y*w+x)*3;if(idx+3>blen)return;const o=base+idx;
    let v;v=m[o]+((r*c+0.5)|0);m[o]=v>255?255:v;v=m[o+1]+((g*c+0.5)|0);m[o+1]=v>255?255:v;
    v=m[o+2]+((b*c+0.5)|0);m[o+2]=v>255?255:v;
  }
  function host_m_blend(ptr,w,h,fx,fy,rgb){
    const m=mem();if(ptr<0||ptr>m.length||w<=0||h<=0)return;const blen=m.length-ptr;
    const r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    const ix=Math.floor(fx),iy=Math.floor(fy),dx=fx-ix,dy=fy-iy;
    aa_add(m,ptr,blen,w,h,ix,iy,r,g,b,(1-dx)*(1-dy));aa_add(m,ptr,blen,w,h,ix+1,iy,r,g,b,dx*(1-dy));
    aa_add(m,ptr,blen,w,h,ix,iy+1,r,g,b,(1-dx)*dy);aa_add(m,ptr,blen,w,h,ix+1,iy+1,r,g,b,dx*dy);
  }
  function host_m_line(ptr,w,h,x0,y0,x1,y1,rgb){
    const m=mem();if(ptr<0||ptr>m.length||w<=0||h<=0)return;const blen=m.length-ptr;
    const r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    let steep=Math.abs(y1-y0)>Math.abs(x1-x0),t;
    if(steep){t=x0;x0=y0;y0=t;t=x1;x1=y1;y1=t;}if(x0>x1){t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    const dx=x1-x0,dy=y1-y0,grad=dx===0?1:dy/dx;
    const pl=(a,bb,c)=>{if(steep)aa_add(m,ptr,blen,w,h,bb,a,r,g,b,c);else aa_add(m,ptr,blen,w,h,a,bb,r,g,b,c);};
    let xend=Math.floor(x0+0.5),yend=y0+grad*(xend-x0),xgap=1-((x0+0.5)-Math.floor(x0+0.5));
    let xp1=xend,yp1=Math.floor(yend),fyy=yend-Math.floor(yend);pl(xp1,yp1,(1-fyy)*xgap);pl(xp1,yp1+1,fyy*xgap);
    let intery=yend+grad;xend=Math.floor(x1+0.5);yend=y1+grad*(xend-x1);xgap=(x1+0.5)-Math.floor(x1+0.5);
    let xp2=xend,yp2=Math.floor(yend);fyy=yend-Math.floor(yend);pl(xp2,yp2,(1-fyy)*xgap);pl(xp2,yp2+1,fyy*xgap);
    for(let x=xp1+1;x<xp2;x++){const iy=Math.floor(intery),f=intery-iy;pl(x,iy,1-f);pl(x,iy+1,f);intery+=grad;}
  }
  return {
    get_width:()=>W, get_height:()=>H, set_pixel:()=>{}, draw:()=>{},
    get_param_i32:(id)=>params[id]??0, get_param_f32:(id)=>params[id]??0, set_param_i32:()=>{},
    m_sin:Math.sin,m_cos:Math.cos,m_sqrt:Math.sqrt,m_hypot:(x,y)=>Math.sqrt(x*x+y*y),
    m_atan2:Math.atan2,m_exp:Math.exp,m_pow:Math.pow,m_hsv:host_m_hsv,
    m_fade:()=>{},m_fill:()=>{},m_noise_fill:()=>{},m_blend:host_m_blend,m_line:host_m_line,
  };
}

function lum(fb,o){ return (fb[o]*77+fb[o+1]*150+fb[o+2]*29)>>8; }

async function testProg(slug, params){
  const bytes = readFileSync(`programs/${slug}/main.wasm`);
  let exp;
  const host = makeHost(()=>exp.memory, params);
  const { instance } = await WebAssembly.instantiate(bytes, { env: host });
  exp = instance.exports;
  const fbPtr = exp.get_framebuffer();
  exp.init?.();
  const snap = (tick)=>{ exp.update(tick); const m=new Uint8Array(exp.memory.buffer); return m.slice(fbPtr, fbPtr+W*H*3); };

  const f0 = snap(0), f1 = snap(600);
  // animation: mean abs luminance diff between frames
  let animDiff=0, maxL=0;
  for(let i=0;i<W*H;i++){ const a=lum(f0,i*3), b=lum(f1,i*3); animDiff+=Math.abs(a-b); maxL=Math.max(maxL,a,b); }
  animDiff/=(W*H);

  // seam continuity on frame f1: per row, mean adjacent-column luminance delta vs seam delta
  let interior=0, interiorN=0, seam=0;
  for(let y=0;y<H;y++){
    for(let x=0;x<W;x++){
      const xn=(x+1)%W;
      const o1=(y*W+x)*3, o2=(y*W+xn)*3;
      const d=Math.abs(lum(f1,o1)-lum(f1,o2));
      if(x===W-1) seam+=d; else { interior+=d; interiorN++; }
    }
  }
  const meanInterior = interior/interiorN;
  const meanSeam = seam/H;
  const ratio = meanInterior>0.5 ? (meanSeam/meanInterior) : (meanSeam<1?0:99);
  console.log(`${slug.padEnd(11)} maxLum=${String(maxL).padStart(3)} anim=${animDiff.toFixed(1).padStart(5)} `+
    `seamDelta=${meanSeam.toFixed(1).padStart(5)} interiorDelta=${meanInterior.toFixed(1).padStart(5)} `+
    `seam/interior=${ratio.toFixed(2)}  ${ratio<=2.0&&animDiff>1&&maxL>40?'OK':'CHECK'}`);
}

await testProg('tunnel',    {0:60,1:220,2:0,3:3});
await testProg('barberpole',{0:60,1:220,2:1,3:2,4:3});
await testProg('ascension', {0:60,1:220,2:0,3:5,4:4});
await testProg('mandala',   {0:60,1:220,2:0,3:6,4:4});
