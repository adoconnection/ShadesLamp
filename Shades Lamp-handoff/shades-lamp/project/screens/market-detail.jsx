// Marketplace item detail with install/upload progress

function MarketDetailScreen({ item, installed, onBack, onInstalled }) {
  // 'idle' | 'downloading' | 'uploading' | 'verifying' | 'done'
  const [phase, setPhase] = React.useState(installed ? 'done' : 'idle');
  const [progress, setProgress] = React.useState(installed ? 1 : 0);

  const startInstall = () => {
    setPhase('downloading'); setProgress(0);
  };

  React.useEffect(() => {
    if (phase === 'idle' || phase === 'done') return;
    let raf;
    const step = () => {
      setProgress(p => {
        const speeds = { downloading: 0.018, uploading: 0.012, verifying: 0.04 };
        const next = p + speeds[phase];
        if (next >= 1) {
          if (phase === 'downloading') { setPhase('uploading'); return 0; }
          if (phase === 'uploading')  { setPhase('verifying'); return 0; }
          if (phase === 'verifying')  { setPhase('done'); onInstalled?.(item); return 1; }
        }
        return next;
      });
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [phase]);

  const accent = item.pulse;
  const installing = phase !== 'idle' && phase !== 'done';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      <div style={{
        position: 'relative',
        background: `linear-gradient(${item.cover.angle}deg, ${item.cover.from}, ${item.cover.via || item.cover.to}, ${item.cover.to})`,
        padding: '54px 20px 28px',
      }}>
        <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(circle at 30% 20%, ${item.cover.via || item.cover.from}80, transparent 60%)`, animation: 'coverFlow1 7s ease-in-out infinite', mixBlendMode: 'screen' }} />
        <div style={{ position: 'absolute', inset: 0, background: 'linear-gradient(180deg, transparent 50%, #0E0D0B)' }} />

        <div style={{ position: 'relative', display: 'flex', justifyContent: 'space-between' }}>
          <NavButton icon={Icons.back} onClick={onBack} />
          <NavButton icon={Icons.more} />
        </div>

        <div style={{ position: 'relative', marginTop: 50 }}>
          <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1, textTransform: 'uppercase', color: 'rgba(255,255,255,0.7)' }}>
            {item.category} · by {item.author}
          </div>
          <div style={{ fontSize: 36, fontWeight: 800, letterSpacing: -1, lineHeight: 1.05, marginTop: 6 }}>{item.name}</div>
          <div style={{ fontSize: 14, color: 'rgba(255,255,255,0.85)', marginTop: 6 }}>{item.desc}</div>
        </div>
      </div>

      {/* Install button / progress */}
      <div style={{ padding: '0 20px', marginTop: -12, position: 'relative', zIndex: 2 }}>
        {phase === 'idle' && (
          <button onClick={startInstall} style={{
            width: '100%', border: 0, cursor: 'pointer',
            background: accent, color: '#0A0A08',
            padding: '15px', borderRadius: 18,
            fontSize: 15, fontWeight: 700, fontFamily: 'inherit', letterSpacing: -0.2,
            display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
          }}>
            {Icons.download} Install — {item.size}
          </button>
        )}
        {installing && (
          <div style={{ background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, padding: 14 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 13, marginBottom: 8 }}>
              <span style={{ fontWeight: 600 }}>{
                phase === 'downloading' ? 'Downloading from registry'
                : phase === 'uploading' ? 'Uploading to lamp'
                : 'Verifying WASM module'
              }</span>
              <span style={{ fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.6)' }}>{Math.round(progress * 100)}%</span>
            </div>
            <div style={{ height: 6, borderRadius: 3, background: 'rgba(255,255,255,0.08)', overflow: 'hidden' }}>
              <div style={{ height: '100%', width: `${progress * 100}%`, background: accent, transition: 'width 0.1s linear' }} />
            </div>
            <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', marginTop: 10 }}>
              {phase === 'downloading' && '› GET github.com/shades-lamp/registry/blob/main/' + item.id + '.wasm'}
              {phase === 'uploading' && '› BLE write 0xFF04 chunks · MTU 512'}
              {phase === 'verifying' && '› wasm3 m3_LoadModule · extracting metadata'}
            </div>
          </div>
        )}
        {phase === 'done' && (
          <div style={{ background: 'rgba(52,211,153,0.1)', border: '0.5px solid rgba(52,211,153,0.25)', borderRadius: 18, padding: '14px 16px', display: 'flex', alignItems: 'center', gap: 12 }}>
            <div style={{ width: 32, height: 32, borderRadius: 16, background: '#34D399', color: '#0A0A08', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{Icons.check}</div>
            <div>
              <div style={{ fontSize: 14, fontWeight: 600, color: '#34D399' }}>Installed</div>
              <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(52,211,153,0.7)', marginTop: 2 }}>littlefs:/programs/{item.id}.wasm</div>
            </div>
          </div>
        )}
      </div>

      {/* Stats */}
      <div style={{ padding: '24px 20px 12px', display: 'flex', gap: 24 }}>
        <Stat label="RATING" value={item.rating + ' ★'} />
        <Stat label="DOWNLOADS" value={(item.downloads / 1000).toFixed(1) + 'k'} />
        <Stat label="SIZE" value={item.size} />
        <Stat label="PARAMS" value={item.paramCount} />
      </div>

      {/* About */}
      <div style={{ padding: '12px 20px 6px', fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>About</div>
      <div style={{ padding: '0 20px 16px', fontSize: 14, color: 'rgba(250,250,247,0.7)', lineHeight: 1.55 }}>
        {item.desc}. Self-contained WASM module compiled with WASI SDK. Runs at 30 FPS on a 16×32 LED matrix using ~{Math.round(parseFloat(item.size) * 1.4)}KB of PSRAM.
      </div>

      {/* Tech */}
      <div style={{ padding: '8px 20px 6px', fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>Technical</div>
      <div style={{ margin: '0 20px 40px', background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, padding: '6px 16px' }}>
        <TechRow label="Module" value={item.id + '.wasm'} />
        <TechRow label="Imports" value="env.set_pixel, env.draw, env.get_param_*" />
        <TechRow label="Memory" value="1 page (64 KB)" />
        <TechRow label="Target" value="wasm32-unknown" last />
      </div>
    </div>
  );
}

function Stat({ label, value }) {
  return (
    <div style={{ flex: 1 }}>
      <div style={{ fontSize: 10, letterSpacing: 1, color: 'rgba(250,250,247,0.4)', fontFamily: 'JetBrains Mono, monospace' }}>{label}</div>
      <div style={{ fontSize: 16, fontWeight: 600, marginTop: 4, letterSpacing: -0.2 }}>{value}</div>
    </div>
  );
}

function TechRow({ label, value, last }) {
  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      padding: '12px 0', borderBottom: last ? 0 : '0.5px solid rgba(255,255,255,0.06)',
      fontFamily: 'JetBrains Mono, monospace', fontSize: 12,
    }}>
      <span style={{ color: 'rgba(250,250,247,0.5)' }}>{label}</span>
      <span style={{ color: '#FAFAF7' }}>{value}</span>
    </div>
  );
}

window.MarketDetailScreen = MarketDetailScreen;
