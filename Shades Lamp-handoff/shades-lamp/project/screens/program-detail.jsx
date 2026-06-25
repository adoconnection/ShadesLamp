// Program detail — params + live preview area + actions

function ProgramDetailScreen({ program, isActive, isFavorite, onBack, onActivate, onParamChange, onDelete, onToggleFavorite }) {
  const accent = program.pulse;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      {/* Hero header with cover gradient as background */}
      <div style={{
        position: 'relative',
        background: `linear-gradient(${program.cover.angle}deg, ${program.cover.from}, ${program.cover.via || program.cover.to}, ${program.cover.to})`,
        padding: '54px 20px 28px',
      }}>
        <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(circle at 30% 20%, ${program.cover.via || program.cover.from}80, transparent 60%)`, animation: 'coverFlow1 7s ease-in-out infinite', mixBlendMode: 'screen' }} />
        <div style={{ position: 'absolute', inset: 0, background: 'linear-gradient(180deg, transparent 60%, #0E0D0B)' }} />

        <div style={{ position: 'relative', display: 'flex', justifyContent: 'space-between' }}>
          <NavButton icon={Icons.back} onClick={onBack} />
          <div style={{ display: 'flex', gap: 10 }}>
            <NavButton icon={isFavorite ? Icons.starFill : Icons.starOutline} onClick={() => onToggleFavorite(program.id)} active={isFavorite} accent="#FCD34D" />
            <NavButton icon={Icons.more} />
          </div>
        </div>

        <div style={{ position: 'relative', marginTop: 50 }}>
          <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1, textTransform: 'uppercase', color: 'rgba(255,255,255,0.7)' }}>
            ID {String(program.id).padStart(2, '0')} · {program.category}
          </div>
          <div style={{ fontSize: 36, fontWeight: 800, letterSpacing: -1, lineHeight: 1.05, marginTop: 6, textShadow: '0 2px 12px rgba(0,0,0,0.3)' }}>{program.name}</div>
          <div style={{ fontSize: 14, color: 'rgba(255,255,255,0.85)', marginTop: 6 }}>{program.desc}</div>
        </div>
      </div>

      {/* Activate / status row */}
      <div style={{ padding: '0 20px', marginTop: -8, display: 'flex', gap: 10, position: 'relative', zIndex: 2 }}>
        <button onClick={onActivate} disabled={isActive} style={{
          flex: 1, border: 0, cursor: isActive ? 'default' : 'pointer',
          background: isActive ? 'rgba(255,255,255,0.06)' : accent,
          color: isActive ? accent : '#0A0A08',
          padding: '15px 18px', borderRadius: 18,
          fontSize: 15, fontWeight: 700, fontFamily: 'inherit', letterSpacing: -0.2,
          display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
        }}>
          {isActive ? (
            <>
              <span style={{ display: 'inline-flex', gap: 2.5, alignItems: 'flex-end', height: 12 }}>
                {[0,1,2,3].map(i => <div key={i} style={{ width: 2.5, background: 'currentColor', borderRadius: 1, animation: `eqBar 0.${5+i}s ease-in-out infinite alternate`, animationDelay: `${i*0.15}s`, height: 6 }} />)}
              </span>
              Running on Lamp
            </>
          ) : 'Set Active'}
        </button>
      </div>

      {/* Meta strip */}
      <div style={{ padding: '20px 20px 6px', display: 'flex', gap: 24, fontSize: 12, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.55)' }}>
        <MetaCol label="AUTHOR" value={program.author} />
        <MetaCol label="SIZE" value={program.size} />
        <MetaCol label="PARAMS" value={program.params.length} />
      </div>

      {/* Parameters */}
      <div style={{ padding: '20px 20px 8px', fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>Parameters</div>
      <div style={{ padding: '0 20px 24px', display: 'flex', flexDirection: 'column', gap: 14 }}>
        {program.params.map(p => (
          <ParamControl key={p.id} param={p} accent={accent}
            onChange={(v) => onParamChange(program.id, p.id, v)} />
        ))}
      </div>

      {/* Storage / file row */}
      <div style={{ padding: '0 20px 12px', fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>File</div>
      <div style={{ margin: '0 20px 16px', background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, padding: '14px 16px' }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <div>
            <div style={{ fontSize: 14, fontWeight: 600 }}>{String(program.id)}.wasm</div>
            <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.5)', marginTop: 3 }}>littlefs:/programs/{program.id}.wasm · {program.size}</div>
          </div>
          {program.author === 'built-in' && (
            <div style={{ background: 'rgba(255,255,255,0.06)', borderRadius: 8, padding: '4px 8px', fontSize: 10, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1, color: 'rgba(250,250,247,0.7)' }}>BUILT-IN</div>
          )}
        </div>
      </div>

      {/* Danger zone */}
      {program.author !== 'built-in' && (
        <div style={{ padding: '0 20px 40px' }}>
          <button onClick={() => onDelete(program.id)} style={{
            width: '100%', border: 0, cursor: 'pointer',
            background: 'rgba(239,68,68,0.1)',
            color: '#F87171', padding: '14px',
            borderRadius: 16, fontSize: 14, fontWeight: 600,
            fontFamily: 'inherit', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
          }}>
            {Icons.trash} Remove from device
          </button>
        </div>
      )}
    </div>
  );
}

function NavButton({ icon, onClick, active, accent }) {
  return (
    <button onClick={onClick} style={{
      width: 40, height: 40, borderRadius: 20, border: 0, cursor: 'pointer',
      background: active ? (accent || '#FAFAF7') : 'rgba(0,0,0,0.35)', backdropFilter: 'blur(12px)',
      WebkitBackdropFilter: 'blur(12px)',
      color: active ? '#0A0A08' : '#FAFAF7', display: 'flex', alignItems: 'center', justifyContent: 'center',
      transition: 'background 0.2s, color 0.2s',
    }}>{icon}</button>
  );
}

function MetaCol({ label, value }) {
  return (
    <div>
      <div style={{ fontSize: 10, letterSpacing: 1, color: 'rgba(250,250,247,0.4)' }}>{label}</div>
      <div style={{ fontSize: 13, color: '#FAFAF7', marginTop: 2 }}>{value}</div>
    </div>
  );
}

function ParamControl({ param, accent, onChange }) {
  return (
    <div style={{ background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, padding: '14px 16px' }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10 }}>
        <div>
          <div style={{ fontSize: 14, fontWeight: 600, letterSpacing: -0.1 }}>{param.name}</div>
          <div style={{ fontSize: 11, color: 'rgba(250,250,247,0.5)', marginTop: 2 }}>{param.desc}</div>
        </div>
        <div style={{ fontSize: 10, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.4)', letterSpacing: 1 }}>
          {param.type.toUpperCase()}{param.type !== 'bool' && param.type !== 'select' ? ` ${param.min}–${param.max}` : ''}
        </div>
      </div>
      {param.type === 'int' && (
        <Slider value={param.value} min={param.min} max={param.max} step={1} color={accent} onChange={onChange} />
      )}
      {param.type === 'float' && (
        <Slider value={param.value} min={param.min} max={param.max} step={0.01} color={accent}
          onChange={onChange} formatValue={(v) => v.toFixed(2)} />
      )}
      {param.type === 'bool' && (
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', paddingTop: 4 }}>
          <span style={{ fontSize: 13, color: 'rgba(250,250,247,0.7)', fontFamily: 'JetBrains Mono, monospace' }}>{param.value ? 'enabled' : 'disabled'}</span>
          <Toggle value={!!param.value} color={accent} onChange={(v) => onChange(v ? 1 : 0)} />
        </div>
      )}
      {param.type === 'select' && (
        <Picker options={param.options} value={param.value} color={accent} label={param.name} onChange={onChange} />
      )}
    </div>
  );
}

window.ProgramDetailScreen = ProgramDetailScreen;
window.NavButton = NavButton;
