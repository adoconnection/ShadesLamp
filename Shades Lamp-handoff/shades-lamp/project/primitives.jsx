// Reusable visual primitives: covers, sliders, toggles, segmented, status pill

// ── Cover gradient (for program cards) ──
function Cover({ cover, size = 56, radius = 14, animated = false, pulse, style = {} }) {
  const { from, to, via, angle = 135 } = cover;
  const stops = via ? `${from}, ${via}, ${to}` : `${from}, ${to}`;
  return (
    <div style={{
      width: size, height: size, borderRadius: radius,
      background: `linear-gradient(${angle}deg, ${stops})`,
      position: 'relative', overflow: 'hidden', flexShrink: 0,
      boxShadow: animated ? `0 8px 32px ${pulse || from}55, 0 2px 8px rgba(0,0,0,0.4)` : '0 1px 3px rgba(0,0,0,0.4)',
      ...style,
    }}>
      {animated && (
        <>
          <div style={{
            position: 'absolute', inset: '-30%',
            background: `radial-gradient(circle at 30% 30%, ${via || to}aa, transparent 50%)`,
            animation: 'coverFlow1 6s ease-in-out infinite',
            mixBlendMode: 'screen',
          }} />
          <div style={{
            position: 'absolute', inset: '-30%',
            background: `radial-gradient(circle at 70% 70%, ${from}aa, transparent 50%)`,
            animation: 'coverFlow2 8s ease-in-out infinite',
            mixBlendMode: 'screen',
          }} />
          <div style={{
            position: 'absolute', inset: 0,
            background: 'linear-gradient(135deg, rgba(255,255,255,0.18), transparent 40%)',
          }} />
        </>
      )}
    </div>
  );
}

// ── LED Matrix preview (animated little blocks of LEDs) ──
function LedMatrix({ program, w = 16, h = 32, pixel = 8, gap = 1, brightness = 1 }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const canvas = ref.current; if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    canvas.width = (w * pixel + (w - 1) * gap) * dpr;
    canvas.height = (h * pixel + (h - 1) * gap) * dpr;
    canvas.style.width = `${w * pixel + (w - 1) * gap}px`;
    canvas.style.height = `${h * pixel + (h - 1) * gap}px`;
    ctx.scale(dpr, dpr);

    let raf, t0 = performance.now();
    const draw = (now) => {
      const t = (now - t0) / 1000;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const c = sampleProgram(program, x, y, w, h, t);
          ctx.fillStyle = `rgba(${c[0]},${c[1]},${c[2]},${brightness})`;
          ctx.fillRect(x * (pixel + gap), y * (pixel + gap), pixel, pixel);
        }
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [program?.id, w, h, pixel, gap, brightness]);
  return <canvas ref={ref} style={{ borderRadius: 6, display: 'block' }} />;
}

// Cheap pseudo-renderers per program type (purely visual, not real WASM)
function sampleProgram(program, x, y, w, h, t) {
  if (!program) return [20, 20, 20];
  switch (program.id) {
    case 0: { // RGB Cycle
      const phase = Math.floor(t * 1.5) % 3;
      return phase === 0 ? [255, 60, 90] : phase === 1 ? [60, 255, 130] : [60, 130, 255];
    }
    case 1: { // Rainbow
      const hue = ((y / h) * 360 + t * 60) % 360;
      return hsvToRgb(hue, 0.9, 1);
    }
    case 2: { // Random blink
      const r = (Math.sin(x * 12.9898 + y * 78.233 + Math.floor(t * 4)) * 43758.5453) % 1;
      const v = Math.abs(r);
      if (v > 0.85) {
        const hue = (v * 720) % 360;
        return hsvToRgb(hue, 0.8, 1);
      }
      return [10, 10, 14];
    }
    case 3: { // Aurora drift
      const a = Math.sin(x * 0.3 + t * 0.6) * 0.5 + 0.5;
      const b = Math.sin(y * 0.2 - t * 0.3 + x * 0.1) * 0.5 + 0.5;
      const hue = 160 + a * 80 + b * 40;
      return hsvToRgb(hue % 360, 0.85, b * 0.9 + 0.1);
    }
    case 4: { // VU meter
      const level = (Math.sin(t * 4) * 0.4 + 0.6 + Math.sin(t * 11) * 0.15);
      const norm = (h - y) / h;
      if (norm > level) return [10, 10, 14];
      const hue = norm < 0.4 ? 120 : norm < 0.7 ? 50 : 0;
      return hsvToRgb(hue, 1, 1);
    }
    case 5: { // Snake
      const sx = Math.floor((Math.sin(t * 0.8) * 0.5 + 0.5) * w);
      const sy = Math.floor((Math.cos(t * 0.6) * 0.5 + 0.5) * h);
      const dist = Math.abs(x - sx) + Math.abs(y - sy);
      if (dist === 0) return [255, 240, 100]; // food
      const tail = Math.sin(t * 3 + x * 0.4 + y * 0.4);
      if (tail > 0.6 && Math.abs(x - sx) < 3 && Math.abs(y - sy) < 3) return [16, 185, 129];
      return [6, 30, 22];
    }
    case 6: { // Candle
      const flicker = Math.sin(t * 8 + x) * 0.1 + Math.sin(t * 13 + y * 0.5) * 0.08 + 0.7;
      const hue = 25 + Math.sin(t * 2 + y * 0.3) * 8;
      return hsvToRgb(hue, 0.9, flicker);
    }
    default: return [40, 40, 40];
  }
}

function hsvToRgb(h, s, v) {
  const c = v * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = v - c;
  let r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)];
}

// ── Slider — thick, music-app style with live value ──
function Slider({ value, min, max, step = 1, color = '#FAFAF7', onChange, formatValue }) {
  const trackRef = React.useRef(null);
  const [drag, setDrag] = React.useState(false);

  const handleMove = (clientX) => {
    const rect = trackRef.current.getBoundingClientRect();
    const ratio = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    let v = min + ratio * (max - min);
    v = Math.round(v / step) * step;
    v = Math.max(min, Math.min(max, v));
    onChange(v);
  };

  const ratio = (value - min) / (max - min);
  return (
    <div
      ref={trackRef}
      onPointerDown={(e) => {
        setDrag(true);
        e.currentTarget.setPointerCapture(e.pointerId);
        handleMove(e.clientX);
      }}
      onPointerMove={(e) => drag && handleMove(e.clientX)}
      onPointerUp={() => setDrag(false)}
      style={{
        height: 44, borderRadius: 22, background: 'rgba(255,255,255,0.06)',
        position: 'relative', cursor: 'pointer', overflow: 'hidden',
        userSelect: 'none', touchAction: 'none',
      }}
    >
      <div style={{
        position: 'absolute', top: 0, bottom: 0, left: 0,
        width: `${ratio * 100}%`,
        background: color,
        borderRadius: 22,
        transition: drag ? 'none' : 'width 0.15s ease',
      }} />
      <div style={{
        position: 'absolute', inset: 0, display: 'flex',
        alignItems: 'center', justifyContent: 'space-between',
        padding: '0 16px', pointerEvents: 'none',
        fontFamily: 'JetBrains Mono, monospace', fontSize: 13, fontWeight: 500,
      }}>
        <span style={{
          color: ratio > 0.15 ? '#0A0A08' : 'rgba(250,250,247,0.6)',
          mixBlendMode: ratio > 0.15 ? 'normal' : 'normal',
        }}>{formatValue ? formatValue(value) : value}</span>
        <span style={{ color: 'rgba(250,250,247,0.35)', fontSize: 11 }}>{max}</span>
      </div>
    </div>
  );
}

// ── Toggle ──
function Toggle({ value, color = '#FAFAF7', onChange }) {
  return (
    <div
      onClick={() => onChange(!value)}
      style={{
        width: 52, height: 32, borderRadius: 16,
        background: value ? color : 'rgba(255,255,255,0.1)',
        position: 'relative', cursor: 'pointer',
        transition: 'background 0.2s ease',
      }}
    >
      <div style={{
        position: 'absolute', top: 3, left: value ? 23 : 3,
        width: 26, height: 26, borderRadius: '50%',
        background: value ? '#0A0A08' : '#FAFAF7',
        transition: 'left 0.2s cubic-bezier(0.4, 1.4, 0.6, 1)',
        boxShadow: '0 1px 3px rgba(0,0,0,0.3)',
      }} />
    </div>
  );
}

// ── Segmented control (for select params) ──
function Segmented({ options, value, color = '#FAFAF7', onChange }) {
  return (
    <div style={{
      display: 'flex', background: 'rgba(255,255,255,0.06)',
      borderRadius: 12, padding: 3, gap: 2,
    }}>
      {options.map((opt, i) => (
        <button
          key={i}
          onClick={() => onChange(i)}
          style={{
            flex: 1, padding: '8px 10px', border: 0,
            background: value === i ? color : 'transparent',
            color: value === i ? '#0A0A08' : 'rgba(250,250,247,0.65)',
            borderRadius: 10, fontSize: 13, fontWeight: 600,
            fontFamily: 'inherit', cursor: 'pointer',
            transition: 'all 0.15s ease',
            letterSpacing: -0.1,
          }}
        >{opt}</button>
      ))}
    </div>
  );
}

// ── Picker — bottom-sheet style for select with many options ──
function Picker({ options, value, color = '#FAFAF7', onChange, label }) {
  const [open, setOpen] = React.useState(false);
  const current = options[value];
  const many = options.length > 4;
  if (!many) {
    return <Segmented options={options} value={value} color={color} onChange={onChange} />;
  }
  return (
    <>
      <button onClick={() => setOpen(true)} style={{
        width: '100%', border: 0, cursor: 'pointer',
        background: 'rgba(255,255,255,0.06)', borderRadius: 12,
        padding: '12px 14px', display: 'flex', alignItems: 'center', justifyContent: 'space-between',
        color: '#FAFAF7', fontFamily: 'inherit', fontSize: 14, fontWeight: 500,
      }}>
        <span style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
          {current}
        </span>
        <span style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)' }}>{value + 1}/{options.length}</span>
          <span style={{ color: 'rgba(250,250,247,0.5)' }}>{Icons.chevron}</span>
        </span>
      </button>
      {open && (
        <div onClick={() => setOpen(false)} style={{
          position: 'absolute', inset: 0, zIndex: 100,
          background: 'rgba(0,0,0,0.5)', backdropFilter: 'blur(6px)',
          display: 'flex', alignItems: 'flex-end',
          animation: 'fadeIn 0.2s ease',
        }}>
          <div onClick={(e) => e.stopPropagation()} style={{
            width: '100%', background: '#1A1815',
            borderTopLeftRadius: 28, borderTopRightRadius: 28,
            paddingBottom: 30, maxHeight: '70%', display: 'flex', flexDirection: 'column',
            animation: 'modalSlideUp 0.3s cubic-bezier(0.32, 0.72, 0, 1)',
          }}>
            <div style={{ display: 'flex', justifyContent: 'center', padding: '10px 0 4px' }}>
              <div style={{ width: 36, height: 4, borderRadius: 2, background: 'rgba(255,255,255,0.2)' }} />
            </div>
            <div style={{ padding: '10px 20px 8px', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <div>
                <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>SELECT</div>
                <div style={{ fontSize: 20, fontWeight: 700, letterSpacing: -0.3 }}>{label}</div>
              </div>
              <button onClick={() => setOpen(false)} style={{
                border: 0, background: 'rgba(255,255,255,0.06)',
                width: 32, height: 32, borderRadius: 16, color: '#FAFAF7',
                display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer',
              }}>{Icons.close}</button>
            </div>
            <div style={{ overflowY: 'auto', padding: '4px 12px 8px' }}>
              {options.map((opt, i) => (
                <button key={i} onClick={() => { onChange(i); setOpen(false); }} style={{
                  width: '100%', border: 0, cursor: 'pointer',
                  background: i === value ? color + '15' : 'transparent',
                  color: '#FAFAF7', fontFamily: 'inherit',
                  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                  padding: '13px 14px', borderRadius: 12, fontSize: 14, textAlign: 'left',
                }}>
                  <span style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <span style={{
                      width: 22, height: 22, borderRadius: 11,
                      border: i === value ? `0` : '1.5px solid rgba(255,255,255,0.2)',
                      background: i === value ? color : 'transparent',
                      color: '#0A0A08', display: 'flex', alignItems: 'center', justifyContent: 'center',
                    }}>{i === value && <span style={{ width: 10, height: 10, borderRadius: 5, background: '#0A0A08' }} />}</span>
                    {opt}
                  </span>
                  <span style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.4)' }}>{i}</span>
                </button>
              ))}
            </div>
          </div>
        </div>
      )}
    </>
  );
}

// ── Status pill (BLE indicator in header) ──
function BleStatus({ state, name, onClick }) {
  // state: 'connected' | 'connecting' | 'disconnected'
  const dot = state === 'connected' ? '#34D399' : state === 'connecting' ? '#FBBF24' : '#71717A';
  const label = state === 'connected' ? name : state === 'connecting' ? 'Подключение…' : 'Не подключено';
  return (
    <button onClick={onClick} style={{
      display: 'flex', alignItems: 'center', gap: 8,
      padding: '7px 12px 7px 10px', border: 0, cursor: 'pointer',
      background: 'rgba(255,255,255,0.06)', borderRadius: 999,
      color: '#FAFAF7', fontFamily: 'inherit', fontSize: 12,
      fontWeight: 500, letterSpacing: -0.1,
    }}>
      <span style={{
        width: 7, height: 7, borderRadius: '50%', background: dot,
        boxShadow: state === 'connected' ? `0 0 8px ${dot}` : 'none',
        animation: state === 'connecting' ? 'blePulse 1.2s ease-in-out infinite' : 'none',
      }} />
      <span>{label}</span>
    </button>
  );
}

// ── Tiny icon set ──
const Icons = {
  back: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><path d="M15 6l-6 6 6 6" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  close: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><path d="M6 6l12 12M18 6l-12 12" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/></svg>,
  more: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><circle cx="5" cy="12" r="1.6" fill="currentColor"/><circle cx="12" cy="12" r="1.6" fill="currentColor"/><circle cx="19" cy="12" r="1.6" fill="currentColor"/></svg>,
  search: <svg width="20" height="20" viewBox="0 0 24 24" fill="none"><circle cx="11" cy="11" r="7" stroke="currentColor" strokeWidth="2"/><path d="M20 20l-3.5-3.5" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/></svg>,
  library: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><rect x="3" y="3" width="7" height="7" rx="1.5" stroke="currentColor" strokeWidth="1.7"/><rect x="14" y="3" width="7" height="7" rx="1.5" stroke="currentColor" strokeWidth="1.7"/><rect x="3" y="14" width="7" height="7" rx="1.5" stroke="currentColor" strokeWidth="1.7"/><rect x="14" y="14" width="7" height="7" rx="1.5" stroke="currentColor" strokeWidth="1.7"/></svg>,
  market: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><path d="M3 7l1.5 11a2 2 0 002 1.7h11a2 2 0 002-1.7L21 7M3 7h18M3 7l2-3h14l2 3M9 11v4M15 11v4" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  settings: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="3" stroke="currentColor" strokeWidth="1.7"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round"/></svg>,
  upload: <svg width="20" height="20" viewBox="0 0 24 24" fill="none"><path d="M12 16V4M6 10l6-6 6 6M4 20h16" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  download: <svg width="18" height="18" viewBox="0 0 24 24" fill="none"><path d="M12 4v12M6 10l6 6 6-6M4 20h16" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  trash: <svg width="20" height="20" viewBox="0 0 24 24" fill="none"><path d="M4 7h16M9 7V4h6v3M6 7l1 13a2 2 0 002 2h6a2 2 0 002-2l1-13M10 11v7M14 11v7" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  star: <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2l3 7 7 .8-5 5L18 22l-6-3.5L6 22l1-7-5-5L9 9z"/></svg>,
  check: <svg width="20" height="20" viewBox="0 0 24 24" fill="none"><path d="M5 13l4 4 10-12" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  bluetooth: <svg width="18" height="18" viewBox="0 0 24 24" fill="none"><path d="M7 7l10 10-5 5V2l5 5L7 17" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  chevron: <svg width="16" height="16" viewBox="0 0 24 24" fill="none"><path d="M9 6l6 6-6 6" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  refresh: <svg width="18" height="18" viewBox="0 0 24 24" fill="none"><path d="M21 12a9 9 0 11-3-6.7L21 8M21 3v5h-5" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round"/></svg>,
  starOutline: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><path d="M12 2.8l2.8 6.5 7 .7-5.3 4.7L18 21.4l-6-3.5-6 3.5 1.5-6.7L2.2 10l7-.7z" stroke="currentColor" strokeWidth="1.7" strokeLinejoin="round"/></svg>,
  starFill: <svg width="22" height="22" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2.8l2.8 6.5 7 .7-5.3 4.7L18 21.4l-6-3.5-6 3.5 1.5-6.7L2.2 10l7-.7z"/></svg>,
  heart: <svg width="22" height="22" viewBox="0 0 24 24" fill="none"><path d="M12 20s-7-4.5-9-9.5C1.5 6.8 4 4 7 4c2 0 3.5 1 5 3 1.5-2 3-3 5-3 3 0 5.5 2.8 4 6.5-2 5-9 9.5-9 9.5z" stroke="currentColor" strokeWidth="1.7" strokeLinejoin="round"/></svg>,
  signal: (rssi) => {
    const bars = rssi > -55 ? 4 : rssi > -70 ? 3 : rssi > -85 ? 2 : 1;
    return (
      <svg width="18" height="14" viewBox="0 0 18 14" fill="none">
        {[1,2,3,4].map(i => (
          <rect key={i} x={(i-1)*4 + 1} y={14 - i*3} width="2.5" height={i*3} rx="0.5"
            fill={i <= bars ? 'currentColor' : 'rgba(255,255,255,0.18)'} />
        ))}
      </svg>
    );
  },
};

Object.assign(window, { Cover, LedMatrix, sampleProgram, hsvToRgb, Slider, Toggle, Segmented, Picker, BleStatus, Icons });
