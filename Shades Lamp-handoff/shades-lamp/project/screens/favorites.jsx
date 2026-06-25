// Favorites screen — list of starred programs

function FavoritesScreen({ programs, favorites, active, onBack, onOpenProgram, onSelectProgram, onOpenMarket }) {
  const items = programs.filter(p => favorites.includes(p.id));

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      <div style={{
        position: 'sticky', top: 0, zIndex: 10,
        padding: '54px 20px 12px',
        background: 'linear-gradient(180deg, #0E0D0B 70%, rgba(14,13,11,0))',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      }}>
        <NavButton icon={Icons.back} onClick={onBack} />
        <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)', fontFamily: 'JetBrains Mono, monospace' }}>{items.length} starred</div>
      </div>

      {/* Hero */}
      <div style={{ padding: '8px 20px 20px' }}>
        <div style={{
          position: 'relative', borderRadius: 28, overflow: 'hidden',
          background: 'linear-gradient(135deg, #FCD34D, #F59E0B 50%, #B45309)',
          padding: '28px 22px 26px', minHeight: 140,
        }}>
          <div style={{ position: 'absolute', inset: 0, background: 'radial-gradient(circle at 80% 20%, rgba(255,255,255,0.3), transparent 60%)', mixBlendMode: 'screen', animation: 'coverFlow1 8s ease-in-out infinite' }} />
          <div style={{ position: 'relative', display: 'flex', alignItems: 'center', gap: 16 }}>
            <div style={{
              width: 56, height: 56, borderRadius: 18,
              background: 'rgba(0,0,0,0.25)', backdropFilter: 'blur(8px)',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              color: '#FAFAF7',
            }}>{Icons.starFill}</div>
            <div>
              <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1, textTransform: 'uppercase', color: 'rgba(0,0,0,0.65)' }}>YOUR COLLECTION</div>
              <div style={{ fontSize: 30, fontWeight: 800, letterSpacing: -0.7, color: '#0A0A08', marginTop: 2 }}>Favorites</div>
            </div>
          </div>
          <div style={{ position: 'relative', fontSize: 13, color: 'rgba(0,0,0,0.7)', marginTop: 14, lineHeight: 1.5 }}>
            Quick access to your starred programs. Tap the star on any program to add it here.
          </div>
        </div>
      </div>

      {items.length === 0 ? (
        <div style={{ padding: '30px 24px', textAlign: 'center' }}>
          <div style={{ width: 72, height: 72, margin: '0 auto 16px', borderRadius: 36, background: 'rgba(252,211,77,0.1)', color: '#FCD34D', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 32 }}>{Icons.starOutline}</div>
          <div style={{ fontSize: 16, fontWeight: 600, marginBottom: 6 }}>No favorites yet</div>
          <div style={{ fontSize: 13, color: 'rgba(250,250,247,0.5)', lineHeight: 1.5, marginBottom: 18 }}>Open any program and tap the star to pin it here for quick access.</div>
          <button onClick={onOpenMarket} style={{
            border: 0, cursor: 'pointer', background: 'rgba(255,255,255,0.06)',
            color: '#FAFAF7', padding: '11px 18px', borderRadius: 999,
            fontSize: 13, fontWeight: 600, fontFamily: 'inherit',
            display: 'inline-flex', alignItems: 'center', gap: 8,
          }}>{Icons.market} Browse marketplace</button>
        </div>
      ) : (
        <div style={{ padding: '0 12px 60px' }}>
          {items.map(p => (
            <ProgramRow key={p.id} program={p} active={p.id === active}
              onTap={() => onSelectProgram(p.id)}
              onOpen={() => onOpenProgram(p)} />
          ))}
        </div>
      )}
    </div>
  );
}

window.FavoritesScreen = FavoritesScreen;
