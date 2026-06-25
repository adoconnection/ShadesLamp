// Main app — navigation, state management, screen transitions

const transitionStyles = `
@keyframes coverFlow1 { 0%, 100% { transform: translate(0, 0) scale(1); } 50% { transform: translate(8%, -6%) scale(1.15); } }
@keyframes coverFlow2 { 0%, 100% { transform: translate(0, 0) scale(1.05); } 50% { transform: translate(-6%, 8%) scale(1.2); } }
@keyframes blePulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.5; transform: scale(1.4); } }
@keyframes bleScan { 0% { transform: scale(0.4); opacity: 0.9; } 100% { transform: scale(1.6); opacity: 0; } }
@keyframes eqBar { from { transform: scaleY(0.3); transform-origin: bottom; } to { transform: scaleY(1); transform-origin: bottom; } }
@keyframes screenSlideIn { from { transform: translateX(100%); } to { transform: translateX(0); } }
@keyframes screenSlideOut { from { transform: translateX(0); } to { transform: translateX(-30%); opacity: 0.5; } }
@keyframes modalSlideUp { from { transform: translateY(100%); } to { transform: translateY(0); } }
@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }

* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; background: #050505; font-family: 'Inter Tight', -apple-system, system-ui, sans-serif; -webkit-font-smoothing: antialiased; }
input::placeholder { color: rgba(250,250,247,0.4); }
::-webkit-scrollbar { display: none; }
button:active { transform: scale(0.98); }
`;

function ShadesApp() {
  const tweakDefaults = /*EDITMODE-BEGIN*/{
    "showBezel": true,
    "wallpaper": "studio",
    "accentMode": "perProgram",
    "fixedAccent": "#60A5FA"
  }/*EDITMODE-END*/;

  const [tweaks, setTweak] = useTweaks(tweakDefaults);

  // App state
  const [programs, setPrograms] = React.useState(PROGRAMS_INSTALLED);
  const [activeId, setActiveId] = React.useState(1); // Rainbow active by default
  const [favorites, setFavorites] = React.useState([1, 3, 6]);
  const [bleState, setBleState] = React.useState('connected');
  const [bleMac, setBleMac] = React.useState('C8:C9:A3:7F:22:04');

  const toggleFavorite = (id) => setFavorites(f => f.includes(id) ? f.filter(x => x !== id) : [...f, id]);

  // Stack-based nav
  const [stack, setStack] = React.useState([{ name: 'library' }]);
  const top = stack[stack.length - 1];
  const push = (screen) => setStack(s => [...s, screen]);
  const pop = () => setStack(s => s.length > 1 ? s.slice(0, -1) : s);

  const handleSelectProgram = (id) => setActiveId(id);
  const handleParamChange = (programId, paramId, value) => {
    setPrograms(ps => ps.map(p =>
      p.id === programId
        ? { ...p, params: p.params.map(prm => prm.id === paramId ? { ...prm, value } : prm) }
        : p
    ));
  };
  const handleDelete = (id) => {
    setPrograms(ps => ps.filter(p => p.id !== id));
    if (activeId === id) setActiveId(0);
    pop();
  };
  const handleInstall = (item) => {
    if (programs.find(p => p.id === item.id)) return;
    const newP = {
      id: item.id,
      name: item.name,
      desc: item.desc,
      author: item.author,
      size: item.size,
      cover: item.cover,
      pulse: item.pulse,
      category: item.category,
      params: [
        { id: 0, name: 'Speed',      type: 'int', min: 1, max: 100, default: 50, value: 50, desc: 'Скорость' },
        { id: 1, name: 'Brightness', type: 'int', min: 1, max: 255, default: 180, value: 180, desc: 'Яркость' },
        { id: 2, name: 'Intensity',  type: 'float', min: 0, max: 1, default: 0.7, value: 0.7, desc: 'Интенсивность' },
      ].slice(0, item.paramCount || 3),
    };
    setPrograms(ps => [...ps, newP]);
  };

  const installedIds = programs.map(p => p.id);

  // Render current screen
  const renderScreen = () => {
    if (top.name === 'library') {
      return <LibraryScreen
        programs={programs} active={activeId} favorites={favorites}
        bleState={bleState}
        onOpenProgram={(p) => push({ name: 'program', id: p.id })}
        onSelectProgram={handleSelectProgram}
        onOpenMarket={() => push({ name: 'market' })}
        onOpenFavorites={() => push({ name: 'favorites' })}
        onOpenSettings={() => push({ name: 'settings' })}
        onOpenBle={() => push({ name: 'ble' })}
      />;
    }
    if (top.name === 'favorites') {
      return <FavoritesScreen
        programs={programs} favorites={favorites} active={activeId}
        onBack={pop}
        onOpenProgram={(p) => push({ name: 'program', id: p.id })}
        onSelectProgram={handleSelectProgram}
        onOpenMarket={() => { pop(); setTimeout(() => push({ name: 'market' }), 100); }}
      />;
    }
    if (top.name === 'program') {
      const p = programs.find(x => x.id === top.id);
      if (!p) { pop(); return null; }
      return <ProgramDetailScreen
        program={p} isActive={p.id === activeId}
        isFavorite={favorites.includes(p.id)}
        onBack={pop}
        onActivate={() => setActiveId(p.id)}
        onParamChange={handleParamChange}
        onDelete={handleDelete}
        onToggleFavorite={toggleFavorite}
      />;
    }
    if (top.name === 'market') {
      return <MarketplaceScreen
        catalog={MARKETPLACE} installedIds={installedIds}
        bleState={bleState}
        onBack={pop}
        onOpenItem={(item) => push({ name: 'market-detail', id: item.id })}
        onOpenBle={() => push({ name: 'ble' })}
      />;
    }
    if (top.name === 'market-detail') {
      const item = MARKETPLACE.find(x => x.id === top.id);
      return <MarketDetailScreen
        item={item} installed={installedIds.includes(item.id)}
        onBack={pop}
        onInstalled={handleInstall}
      />;
    }
    if (top.name === 'ble') {
      return <BleScreen
        devices={NEARBY_DEVICES} currentMac={bleMac}
        onBack={pop}
        onConnect={(mac) => {
          setBleState('connecting');
          setTimeout(() => { setBleMac(mac); setBleState('connected'); }, 1500);
        }}
      />;
    }
    if (top.name === 'settings') {
      return <SettingsScreen
        info={DEVICE_INFO} programCount={programs.length}
        onBack={pop}
        onOpenBle={() => push({ name: 'ble' })}
      />;
    }
    return null;
  };

  // wallpapers
  const wallpapers = {
    studio: 'radial-gradient(ellipse at 30% 20%, #1A1815, #050504 60%)',
    night:  'radial-gradient(ellipse at 70% 30%, #0A1428, #04060B 60%)',
    flat:   '#0A0A09',
  };

  const screen = (
    <div key={stack.length} style={{
      width: '100%', height: '100%', position: 'relative', overflow: 'hidden',
      animation: stack.length > 1 ? 'screenSlideIn 0.32s cubic-bezier(0.32, 0.72, 0, 1)' : 'fadeIn 0.3s ease',
    }}>
      {renderScreen()}
    </div>
  );

  const content = tweaks.showBezel ? (
    <div style={{
      minHeight: '100vh', width: '100%',
      background: wallpapers[tweaks.wallpaper] || wallpapers.studio,
      display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 24,
    }}>
      <IOSDevice width={402} height={874} dark={true}>
        {screen}
      </IOSDevice>
    </div>
  ) : (
    <div style={{ width: '100vw', height: '100vh', maxWidth: 480, margin: '0 auto' }}>{screen}</div>
  );

  return (
    <>
      <style>{transitionStyles}</style>
      {content}
      <TweaksPanel title="Tweaks">
        <TweakSection title="Frame">
          <TweakToggle label="iPhone bezel" value={tweaks.showBezel} onChange={(v) => setTweak('showBezel', v)} />
          <TweakRadio label="Wallpaper" value={tweaks.wallpaper}
            options={[{ label: 'Studio', value: 'studio' }, { label: 'Night', value: 'night' }, { label: 'Flat', value: 'flat' }]}
            onChange={(v) => setTweak('wallpaper', v)} />
        </TweakSection>
        <TweakSection title="Quick jump">
          <TweakButton label="→ Library"          onClick={() => setStack([{ name: 'library' }])} />
          <TweakButton label="→ Program detail"   onClick={() => setStack([{ name: 'library' }, { name: 'program', id: activeId }])} />
          <TweakButton label="→ Marketplace"      onClick={() => setStack([{ name: 'library' }, { name: 'market' }])} />
          <TweakButton label="→ Market detail"    onClick={() => setStack([{ name: 'library' }, { name: 'market' }, { name: 'market-detail', id: 'm-plasma' }])} />
          <TweakButton label="→ Favorites"        onClick={() => setStack([{ name: 'library' }, { name: 'favorites' }])} />
          <TweakButton label="→ BLE connect"      onClick={() => setStack([{ name: 'library' }, { name: 'ble' }])} />
          <TweakButton label="→ Device settings"  onClick={() => setStack([{ name: 'library' }, { name: 'settings' }])} />
        </TweakSection>
        <TweakSection title="BLE state">
          <TweakRadio label="Connection" value={bleState}
            options={[{ label: 'Online', value: 'connected' }, { label: 'Connecting', value: 'connecting' }, { label: 'Offline', value: 'disconnected' }]}
            onChange={setBleState} />
        </TweakSection>
        <TweakSection title="Active program">
          <TweakSelect label="Now Running" value={activeId}
            options={programs.map(p => ({ label: p.name, value: p.id }))}
            onChange={(v) => setActiveId(Number(v))} />
        </TweakSection>
      </TweaksPanel>
    </>
  );
}

const root = ReactDOM.createRoot(document.getElementById('root'));
root.render(<ShadesApp />);
