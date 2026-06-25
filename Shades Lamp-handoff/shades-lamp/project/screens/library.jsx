// Library screen — currently active program (hero) + list of installed programs

function LibraryScreen({ active, programs, favorites, onOpenProgram, onSelectProgram, onOpenMarket, onOpenFavorites, onOpenSettings, onOpenBle, bleState }) {
  const activeProgram = programs.find(p => p.id === active);
  const favoriteCount = favorites.length;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7' }}>
      {/* Header */}
      <div style={{
        position: 'sticky', top: 0, zIndex: 10,
        padding: '54px 20px 12px',
        background: 'linear-gradient(180deg, #0E0D0B 70%, rgba(14,13,11,0))',
        display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      }}>
        <div>
          <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>SHADES</div>
          <div style={{ fontSize: 22, fontWeight: 700, letterSpacing: -0.5, marginTop: 1 }}>Library</div>
        </div>
        <BleStatus state={bleState} name="Shades Lamp" onClick={onOpenBle} />
      </div>

      {/* Now playing hero */}
      <div style={{ padding: '8px 20px 24px' }}>
        <div
          onClick={() => onOpenProgram(activeProgram)}
          style={{
            position: 'relative', borderRadius: 28, overflow: 'hidden',
            background: `linear-gradient(${activeProgram.cover.angle}deg, ${activeProgram.cover.from}, ${activeProgram.cover.via || activeProgram.cover.to}, ${activeProgram.cover.to})`,
            padding: '20px 20px 18px', cursor: 'pointer',
            boxShadow: `0 20px 50px ${activeProgram.pulse}33, 0 4px 16px rgba(0,0,0,0.5)`,
            minHeight: 220,
          }}
        >
          {/* animated overlay */}
          <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(circle at 30% 20%, ${activeProgram.cover.via || activeProgram.cover.from}80, transparent 60%)`, animation: 'coverFlow1 7s ease-in-out infinite', mixBlendMode: 'screen' }} />
          <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(circle at 80% 80%, ${activeProgram.cover.to}80, transparent 60%)`, animation: 'coverFlow2 9s ease-in-out infinite', mixBlendMode: 'screen' }} />

          <div style={{ position: 'relative', display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <span style={{
                width: 8, height: 8, borderRadius: '50%', background: '#FAFAF7',
                boxShadow: '0 0 8px rgba(255,255,255,0.8)',
                animation: 'blePulse 1.6s ease-in-out infinite',
              }} />
              <span style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1, textTransform: 'uppercase', color: 'rgba(255,255,255,0.85)' }}>NOW RUNNING</span>
            </div>
            <div style={{ background: 'rgba(0,0,0,0.25)', backdropFilter: 'blur(8px)', borderRadius: 999, padding: '5px 10px', fontSize: 11, fontFamily: 'JetBrains Mono, monospace' }}>
              ID {String(activeProgram.id).padStart(2, '0')}
            </div>
          </div>

          <div style={{ position: 'absolute', left: 20, right: 20, bottom: 18 }}>
            <div style={{ fontSize: 32, fontWeight: 800, letterSpacing: -1, lineHeight: 1.05, textShadow: '0 2px 12px rgba(0,0,0,0.3)' }}>{activeProgram.name}</div>
            <div style={{ fontSize: 13, color: 'rgba(255,255,255,0.85)', marginTop: 4, textShadow: '0 1px 4px rgba(0,0,0,0.3)' }}>{activeProgram.desc}</div>
            <div style={{ display: 'flex', gap: 10, marginTop: 14 }}>
              {activeProgram.params.slice(0, 3).map(p => (
                <div key={p.id} style={{
                  background: 'rgba(0,0,0,0.3)', backdropFilter: 'blur(8px)',
                  borderRadius: 10, padding: '6px 10px',
                  fontSize: 11, fontFamily: 'JetBrains Mono, monospace',
                  border: '0.5px solid rgba(255,255,255,0.18)',
                }}>
                  <span style={{ opacity: 0.65 }}>{p.name}</span>
                  <span style={{ marginLeft: 6, fontWeight: 600 }}>{
                    p.type === 'bool' ? (p.value ? 'on' : 'off')
                    : p.type === 'select' ? p.options[p.value]
                    : p.type === 'float' ? p.value.toFixed(2)
                    : p.value
                  }</span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>

      {/* Quick actions */}
      <div style={{ padding: '0 20px 18px', display: 'flex', gap: 10 }}>
        <ActionTile icon={Icons.market} label="Marketplace" onClick={onOpenMarket} />
        <ActionTile icon={Icons.starOutline} label="Favorites" detail={favoriteCount > 0 ? `${favoriteCount}` : null} onClick={onOpenFavorites} />
        <ActionTile icon={Icons.settings} label="Device" onClick={onOpenSettings} />
      </div>

      {/* Section header */}
      <div style={{ padding: '4px 20px 10px', display: 'flex', alignItems: 'baseline', justifyContent: 'space-between' }}>
        <div style={{ fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>Installed</div>
        <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)', fontFamily: 'JetBrains Mono, monospace' }}>{programs.length} / 16</div>
      </div>

      {/* Program list */}
      <div style={{ padding: '0 12px 80px' }}>
        {programs.map(p => (
          <ProgramRow key={p.id} program={p} active={p.id === active}
            isFavorite={favorites.includes(p.id)}
            onTap={() => onSelectProgram(p.id)}
            onOpen={() => onOpenProgram(p)} />
        ))}
      </div>
    </div>
  );
}

function ActionTile({ icon, label, detail, onClick }) {
  return (
    <button onClick={onClick} style={{
      flex: 1, border: 0, cursor: 'pointer',
      background: 'rgba(255,255,255,0.04)',
      border: '0.5px solid rgba(255,255,255,0.06)',
      borderRadius: 18, padding: '14px 10px',
      display: 'flex', flexDirection: 'column', alignItems: 'flex-start', gap: 8,
      color: '#FAFAF7', fontFamily: 'inherit', position: 'relative',
    }}>
      <div style={{ color: 'rgba(250,250,247,0.85)' }}>{icon}</div>
      <div style={{ fontSize: 12, fontWeight: 500, letterSpacing: -0.1 }}>{label}</div>
      {detail && (
        <div style={{
          position: 'absolute', top: 10, right: 10,
          background: '#FCD34D', color: '#0A0A08',
          minWidth: 18, height: 18, borderRadius: 9, padding: '0 5px',
          fontSize: 10, fontWeight: 700, fontFamily: 'JetBrains Mono, monospace',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}>{detail}</div>
      )}
    </button>
  );
}

function ProgramRow({ program, active, isFavorite, onTap, onOpen }) {
  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 12,
      padding: '8px 8px',
      borderRadius: 14,
      background: active ? 'rgba(255,255,255,0.04)' : 'transparent',
      transition: 'background 0.15s',
    }}>
      <div onClick={onTap} style={{ cursor: 'pointer', position: 'relative' }}>
        <Cover cover={program.cover} pulse={program.pulse} size={56} radius={12} animated={active} />
        {active && (
          <div style={{
            position: 'absolute', inset: 0, borderRadius: 12,
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            background: 'rgba(0,0,0,0.35)',
          }}>
            <div style={{ display: 'flex', gap: 2.5, alignItems: 'flex-end', height: 16 }}>
              {[0,1,2,3].map(i => (
                <div key={i} style={{
                  width: 3, background: '#FAFAF7', borderRadius: 1.5,
                  animation: `eqBar 0.${5+i}s ease-in-out infinite alternate`,
                  animationDelay: `${i * 0.15}s`,
                  height: 6 + (i % 2) * 6,
                }} />
              ))}
            </div>
          </div>
        )}
      </div>
      <div onClick={onTap} style={{ flex: 1, cursor: 'pointer', minWidth: 0 }}>
        <div style={{ fontSize: 15, fontWeight: 600, letterSpacing: -0.2, color: active ? program.pulse : '#FAFAF7', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', display: 'flex', alignItems: 'center', gap: 6 }}>
          {program.name}
          {isFavorite && <span style={{ color: '#FCD34D', display: 'inline-flex', flexShrink: 0 }}><svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2.8l2.8 6.5 7 .7-5.3 4.7L18 21.4l-6-3.5-6 3.5 1.5-6.7L2.2 10l7-.7z"/></svg></span>}
        </div>
        <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 2, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {program.author} · {program.params.length} params · {program.size}
        </div>
      </div>
      <button onClick={onOpen} style={{
        border: 0, background: 'rgba(255,255,255,0.06)', cursor: 'pointer',
        width: 36, height: 36, borderRadius: 18, color: '#FAFAF7',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>{Icons.chevron}</button>
    </div>
  );
}

window.LibraryScreen = LibraryScreen;
window.ProgramRow = ProgramRow;
