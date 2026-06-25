// Marketplace screens — featured + categories list, detail with install

function MarketplaceScreen({ catalog, installedIds, onBack, onOpenItem, onOpenBle, bleState }) {
  const [category, setCategory] = React.useState('All');
  const [search, setSearch] = React.useState('');

  const filtered = catalog.filter(item => {
    if (category !== 'All' && item.category !== category) return false;
    if (search && !item.name.toLowerCase().includes(search.toLowerCase()) && !item.author.toLowerCase().includes(search.toLowerCase())) return false;
    return true;
  });
  const featured = catalog.filter(i => i.featured);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      <div style={{
        position: 'sticky', top: 0, zIndex: 10,
        padding: '54px 20px 12px',
        background: 'linear-gradient(180deg, #0E0D0B 70%, rgba(14,13,11,0))',
      }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <NavButton icon={Icons.back} onClick={onBack} />
          <BleStatus state={bleState} name="Shades Lamp" onClick={onOpenBle} />
        </div>
        <div style={{ marginTop: 14 }}>
          <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>github://shades-lamp/registry</div>
          <div style={{ fontSize: 30, fontWeight: 800, letterSpacing: -0.7, marginTop: 4 }}>Marketplace</div>
        </div>

        {/* search */}
        <div style={{ marginTop: 14, display: 'flex', alignItems: 'center', gap: 10, background: 'rgba(255,255,255,0.06)', borderRadius: 14, padding: '11px 14px' }}>
          <span style={{ color: 'rgba(250,250,247,0.5)' }}>{Icons.search}</span>
          <input value={search} onChange={(e) => setSearch(e.target.value)} placeholder="Search programs, authors…"
            style={{ flex: 1, background: 'transparent', border: 0, outline: 'none', color: '#FAFAF7', fontFamily: 'inherit', fontSize: 14 }} />
        </div>
      </div>

      {/* Featured carousel — only show on All category */}
      {category === 'All' && !search && (
        <>
          <div style={{ padding: '14px 20px 10px', display: 'flex', alignItems: 'baseline', justifyContent: 'space-between' }}>
            <div style={{ fontSize: 17, fontWeight: 700, letterSpacing: -0.3 }}>Featured</div>
            <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)' }}>This week</div>
          </div>
          <div style={{ display: 'flex', gap: 12, overflowX: 'auto', padding: '0 20px 4px', scrollbarWidth: 'none' }}>
            {featured.map(item => (
              <FeaturedCard key={item.id} item={item} installed={installedIds.includes(item.id)} onClick={() => onOpenItem(item)} />
            ))}
          </div>
        </>
      )}

      {/* Category tabs */}
      <div style={{ display: 'flex', gap: 8, padding: '20px 20px 6px', overflowX: 'auto', scrollbarWidth: 'none' }}>
        {CATEGORIES.map(cat => (
          <button key={cat} onClick={() => setCategory(cat)}
            style={{
              border: 0, cursor: 'pointer',
              background: category === cat ? '#FAFAF7' : 'rgba(255,255,255,0.06)',
              color: category === cat ? '#0A0A08' : '#FAFAF7',
              padding: '8px 16px', borderRadius: 999,
              fontSize: 13, fontWeight: 600, fontFamily: 'inherit',
              whiteSpace: 'nowrap', letterSpacing: -0.1,
            }}>{cat}</button>
        ))}
      </div>

      {/* Results list */}
      <div style={{ padding: '12px 20px 60px', display: 'flex', flexDirection: 'column', gap: 8 }}>
        {filtered.map(item => (
          <MarketRow key={item.id} item={item} installed={installedIds.includes(item.id)} onClick={() => onOpenItem(item)} />
        ))}
        {filtered.length === 0 && (
          <div style={{ textAlign: 'center', padding: 40, color: 'rgba(250,250,247,0.4)', fontSize: 13 }}>
            No programs match this filter.
          </div>
        )}
      </div>
    </div>
  );
}

function FeaturedCard({ item, installed, onClick }) {
  return (
    <div onClick={onClick} style={{
      flexShrink: 0, width: 220, borderRadius: 22, overflow: 'hidden',
      background: `linear-gradient(${item.cover.angle}deg, ${item.cover.from}, ${item.cover.via || item.cover.to}, ${item.cover.to})`,
      cursor: 'pointer', position: 'relative', minHeight: 260,
      boxShadow: `0 12px 32px ${item.pulse}33`,
    }}>
      <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(circle at 30% 20%, ${item.cover.via || item.cover.from}80, transparent 60%)`, animation: 'coverFlow1 7s ease-in-out infinite', mixBlendMode: 'screen' }} />
      <div style={{ position: 'absolute', inset: 0, background: 'linear-gradient(180deg, transparent 50%, rgba(0,0,0,0.6))' }} />
      <div style={{ position: 'absolute', top: 14, left: 14, right: 14, display: 'flex', justifyContent: 'space-between' }}>
        <div style={{ background: 'rgba(0,0,0,0.35)', backdropFilter: 'blur(8px)', borderRadius: 999, padding: '5px 10px', fontSize: 10, fontFamily: 'JetBrains Mono, monospace', letterSpacing: 1 }}>FEATURED</div>
        {installed && <div style={{ background: 'rgba(0,0,0,0.35)', backdropFilter: 'blur(8px)', borderRadius: 999, width: 26, height: 26, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#34D399' }}>{Icons.check}</div>}
      </div>
      <div style={{ position: 'absolute', left: 16, right: 16, bottom: 14 }}>
        <div style={{ fontSize: 22, fontWeight: 800, letterSpacing: -0.5, lineHeight: 1.05 }}>{item.name}</div>
        <div style={{ fontSize: 12, color: 'rgba(255,255,255,0.85)', marginTop: 4, display: 'flex', alignItems: 'center', gap: 6 }}>
          <span>{item.author}</span>
          <span style={{ opacity: 0.5 }}>·</span>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 3, color: '#FCD34D' }}>{Icons.star} {item.rating}</span>
        </div>
      </div>
    </div>
  );
}

function MarketRow({ item, installed, onClick }) {
  return (
    <div onClick={onClick} style={{
      display: 'flex', alignItems: 'center', gap: 12,
      background: 'rgba(255,255,255,0.04)',
      border: '0.5px solid rgba(255,255,255,0.06)',
      borderRadius: 16, padding: 10, cursor: 'pointer',
    }}>
      <Cover cover={item.cover} pulse={item.pulse} size={56} radius={12} />
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{ fontSize: 15, fontWeight: 600, letterSpacing: -0.2, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{item.name}</div>
          {installed && <span style={{ color: '#34D399', display: 'inline-flex' }}>{Icons.check}</span>}
        </div>
        <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 2, display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ color: '#FCD34D', display: 'inline-flex', alignItems: 'center', gap: 3 }}>{Icons.star} {item.rating}</span>
          <span>·</span>
          <span style={{ fontFamily: 'JetBrains Mono, monospace' }}>{(item.downloads / 1000).toFixed(1)}k</span>
          <span>·</span>
          <span>{item.author}</span>
        </div>
      </div>
      <div style={{
        width: 36, height: 36, borderRadius: 18,
        background: installed ? 'transparent' : item.pulse + '22',
        color: installed ? 'rgba(250,250,247,0.5)' : item.pulse,
        display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0,
      }}>{installed ? Icons.chevron : Icons.download}</div>
    </div>
  );
}

window.MarketplaceScreen = MarketplaceScreen;
