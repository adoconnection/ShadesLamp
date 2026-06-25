// BLE connect, Upload (custom .wasm), Settings — three smaller modal-y screens

// ── BLE Connect ──
function BleScreen({ devices, currentMac, onBack, onConnect }) {
  const [scanning, setScanning] = React.useState(true);
  React.useEffect(() => {
    const t = setTimeout(() => setScanning(false), 2400);
    return () => clearTimeout(t);
  }, []);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      <div style={{ padding: '54px 20px 12px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <NavButton icon={Icons.back} onClick={onBack} />
        <button onClick={() => setScanning(true) || setTimeout(() => setScanning(false), 1800)} style={{
          border: 0, cursor: 'pointer', background: 'rgba(255,255,255,0.06)',
          color: '#FAFAF7', borderRadius: 999, padding: '7px 14px',
          fontSize: 12, fontWeight: 500, fontFamily: 'inherit',
          display: 'flex', alignItems: 'center', gap: 6,
        }}>{Icons.refresh} Rescan</button>
      </div>
      <div style={{ padding: '12px 20px 8px' }}>
        <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>BLE GATT · 0000ff00</div>
        <div style={{ fontSize: 30, fontWeight: 800, letterSpacing: -0.7, marginTop: 4 }}>Devices</div>
      </div>

      {/* Big BLE animation */}
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', padding: '20px 0 30px' }}>
        <div style={{ position: 'relative', width: 120, height: 120, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          {scanning && [0,1,2].map(i => (
            <div key={i} style={{
              position: 'absolute', inset: 0, borderRadius: '50%',
              border: '1px solid rgba(96, 165, 250, 0.4)',
              animation: `bleScan 2.4s ease-out infinite`,
              animationDelay: `${i * 0.8}s`,
            }} />
          ))}
          <div style={{
            width: 64, height: 64, borderRadius: '50%',
            background: 'rgba(96,165,250,0.12)', border: '1px solid rgba(96,165,250,0.3)',
            color: '#60A5FA', display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>{Icons.bluetooth}</div>
        </div>
        <div style={{ fontSize: 13, color: 'rgba(250,250,247,0.6)', marginTop: 8, fontFamily: 'JetBrains Mono, monospace' }}>
          {scanning ? 'Scanning…' : `${devices.length} device${devices.length !== 1 ? 's' : ''} found`}
        </div>
      </div>

      {/* Devices list */}
      <div style={{ padding: '0 20px 40px' }}>
        <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.4)', letterSpacing: 1, marginBottom: 8, paddingLeft: 4 }}>NEARBY</div>
        <div style={{ background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, overflow: 'hidden' }}>
          {devices.map((d, i) => (
            <div key={d.mac} onClick={() => onConnect(d.mac)} style={{
              display: 'flex', alignItems: 'center', gap: 12, padding: '14px 16px',
              cursor: 'pointer',
              borderBottom: i < devices.length - 1 ? '0.5px solid rgba(255,255,255,0.06)' : 0,
            }}>
              <div style={{
                width: 36, height: 36, borderRadius: 10,
                background: 'rgba(96,165,250,0.12)', color: '#60A5FA',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
              }}>{Icons.bluetooth}</div>
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <span style={{ fontSize: 14, fontWeight: 600 }}>{d.name}</span>
                  {d.mac === currentMac && <span style={{ color: '#34D399', fontSize: 11, fontFamily: 'JetBrains Mono, monospace' }}>● connected</span>}
                  {d.paired && d.mac !== currentMac && <span style={{ color: 'rgba(250,250,247,0.45)', fontSize: 11, fontFamily: 'JetBrains Mono, monospace' }}>paired</span>}
                </div>
                <div style={{ fontSize: 11, color: 'rgba(250,250,247,0.45)', marginTop: 2, fontFamily: 'JetBrains Mono, monospace' }}>{d.mac} · {d.rssi} dBm</div>
              </div>
              <div style={{ color: 'rgba(250,250,247,0.5)' }}>{Icons.signal(d.rssi)}</div>
            </div>
          ))}
        </div>
        <div style={{ fontSize: 11, color: 'rgba(250,250,247,0.4)', marginTop: 12, paddingLeft: 4, lineHeight: 1.5, fontFamily: 'JetBrains Mono, monospace' }}>
          Service UUID 0000ff00-0000-1000-8000-00805f9b34fb
        </div>
      </div>
    </div>
  );
}

// ── Upload custom .wasm ──
function UploadScreen({ onBack, onUploaded }) {
  const [phase, setPhase] = React.useState('picking'); // picking | start | uploading | verifying | done | error
  const [progress, setProgress] = React.useState(0);
  const [filename, setFilename] = React.useState('plasma_field.wasm');
  const [filesize, setFilesize] = React.useState(7423);

  const begin = () => { setPhase('start'); setProgress(0); };

  React.useEffect(() => {
    if (phase === 'picking' || phase === 'done' || phase === 'error') return;
    let raf;
    const step = () => {
      setProgress(p => {
        const speed = { start: 0.08, uploading: 0.014, verifying: 0.04 }[phase];
        const next = p + speed;
        if (next >= 1) {
          if (phase === 'start') { setPhase('uploading'); return 0; }
          if (phase === 'uploading') { setPhase('verifying'); return 0; }
          if (phase === 'verifying') { setPhase('done'); return 1; }
        }
        return next;
      });
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [phase]);

  const stepOf = phase === 'start' ? 1 : phase === 'uploading' ? 2 : phase === 'verifying' ? 3 : phase === 'done' ? 4 : 0;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7' }}>
      <div style={{ padding: '54px 20px 8px' }}>
        <NavButton icon={Icons.close} onClick={onBack} />
      </div>
      <div style={{ padding: '14px 20px 20px' }}>
        <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>SIDELOAD</div>
        <div style={{ fontSize: 30, fontWeight: 800, letterSpacing: -0.7, marginTop: 4 }}>Upload .wasm</div>
        <div style={{ fontSize: 14, color: 'rgba(250,250,247,0.55)', marginTop: 6 }}>Send a custom WASM module directly to the lamp over BLE.</div>
      </div>

      {phase === 'picking' ? (
        <div style={{ flex: 1, padding: '0 20px', display: 'flex', flexDirection: 'column', gap: 16 }}>
          <div onClick={begin} style={{
            border: '1.5px dashed rgba(255,255,255,0.18)',
            borderRadius: 22, padding: 32, textAlign: 'center', cursor: 'pointer',
            background: 'rgba(255,255,255,0.02)',
          }}>
            <div style={{ width: 56, height: 56, margin: '0 auto 14px', borderRadius: 16, background: 'rgba(96,165,250,0.12)', color: '#60A5FA', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{Icons.upload}</div>
            <div style={{ fontSize: 15, fontWeight: 600 }}>Choose .wasm file</div>
            <div style={{ fontSize: 12, color: 'rgba(250,250,247,0.5)', marginTop: 4 }}>Up to 64 KB · single linear memory page</div>
          </div>
          <div style={{ background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 16, padding: '14px 16px' }}>
            <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, marginBottom: 6 }}>BUILD WITH</div>
            <pre style={{ margin: 0, fontFamily: 'JetBrains Mono, monospace', fontSize: 11, color: 'rgba(250,250,247,0.75)', lineHeight: 1.55, whiteSpace: 'pre-wrap' }}>{`clang --target=wasm32 -nostdlib -O2 \\
  -Wl,--no-entry -Wl,--export-all \\
  -Wl,--allow-undefined \\
  -o my_program.wasm main.c`}</pre>
          </div>
        </div>
      ) : (
        <div style={{ flex: 1, padding: '0 20px', display: 'flex', flexDirection: 'column' }}>
          {/* File card */}
          <div style={{ background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, padding: '14px 16px', display: 'flex', alignItems: 'center', gap: 12 }}>
            <div style={{ width: 44, height: 44, borderRadius: 10, background: 'linear-gradient(135deg, #60A5FA, #A78BFA)', color: '#0A0A08', display: 'flex', alignItems: 'center', justifyContent: 'center', fontWeight: 800, fontFamily: 'JetBrains Mono, monospace', fontSize: 11 }}>WASM</div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontSize: 14, fontWeight: 600, fontFamily: 'JetBrains Mono, monospace' }}>{filename}</div>
              <div style={{ fontSize: 11, color: 'rgba(250,250,247,0.5)', marginTop: 2, fontFamily: 'JetBrains Mono, monospace' }}>{filesize.toLocaleString()} bytes</div>
            </div>
          </div>

          {/* Steps */}
          <div style={{ marginTop: 24, display: 'flex', flexDirection: 'column', gap: 12 }}>
            {[
              { id: 1, label: 'UPLOAD_START',  hint: 'Allocating PSRAM buffer' },
              { id: 2, label: 'WRITE chunks',  hint: 'BLE WRITE_NR · 512 bytes / chunk' },
              { id: 3, label: 'UPLOAD_FINISH', hint: 'wasm3 validation + meta extract' },
              { id: 4, label: 'Persisted',     hint: 'littlefs:/programs/3.wasm' },
            ].map(s => {
              const state = stepOf > s.id ? 'done' : stepOf === s.id ? 'active' : 'pending';
              return (
                <div key={s.id} style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                  <div style={{
                    width: 28, height: 28, borderRadius: 14,
                    background: state === 'done' ? '#34D399' : state === 'active' ? '#60A5FA' : 'rgba(255,255,255,0.06)',
                    color: state === 'pending' ? 'rgba(250,250,247,0.5)' : '#0A0A08',
                    display: 'flex', alignItems: 'center', justifyContent: 'center',
                    fontFamily: 'JetBrains Mono, monospace', fontSize: 11, fontWeight: 700,
                    flexShrink: 0,
                  }}>{state === 'done' ? Icons.check : s.id}</div>
                  <div style={{ flex: 1 }}>
                    <div style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 13, color: state === 'pending' ? 'rgba(250,250,247,0.4)' : '#FAFAF7' }}>{s.label}</div>
                    <div style={{ fontSize: 11, color: 'rgba(250,250,247,0.4)', marginTop: 1 }}>{s.hint}</div>
                  </div>
                  {state === 'active' && (
                    <div style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 11, color: '#60A5FA' }}>{Math.round(progress * 100)}%</div>
                  )}
                </div>
              );
            })}
          </div>

          {phase === 'uploading' && (
            <div style={{ marginTop: 20, height: 4, borderRadius: 2, background: 'rgba(255,255,255,0.08)', overflow: 'hidden' }}>
              <div style={{ height: '100%', width: `${progress * 100}%`, background: '#60A5FA' }} />
            </div>
          )}

          {phase === 'done' && (
            <button onClick={() => onUploaded({ name: filename })} style={{
              marginTop: 'auto', marginBottom: 30,
              border: 0, cursor: 'pointer', background: '#34D399',
              color: '#0A0A08', padding: 15, borderRadius: 18,
              fontSize: 15, fontWeight: 700, fontFamily: 'inherit',
              display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
            }}>{Icons.check} Done — open program</button>
          )}
        </div>
      )}
    </div>
  );
}

// ── Settings (device) ──
function SettingsScreen({ onBack, onOpenBle, info, programCount }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: '#0E0D0B', color: '#FAFAF7', overflow: 'auto' }}>
      <div style={{ padding: '54px 20px 12px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <NavButton icon={Icons.back} onClick={onBack} />
      </div>
      <div style={{ padding: '12px 20px 16px' }}>
        <div style={{ fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1, textTransform: 'uppercase' }}>ESP32-S3 · WasmLED</div>
        <div style={{ fontSize: 30, fontWeight: 800, letterSpacing: -0.7, marginTop: 4 }}>{info.name}</div>
      </div>

      {/* Hero stats card */}
      <div style={{ margin: '0 20px 20px', borderRadius: 22, padding: 18,
        background: 'linear-gradient(135deg, #1F1D1A, #14130F)',
        border: '0.5px solid rgba(255,255,255,0.06)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ width: 8, height: 8, borderRadius: '50%', background: '#34D399', boxShadow: '0 0 8px #34D399' }} />
          <span style={{ fontSize: 12, fontFamily: 'JetBrains Mono, monospace', color: '#34D399', letterSpacing: 0.5 }}>ONLINE · {info.rssi} dBm</span>
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14, marginTop: 16 }}>
          <StatBlock label="MATRIX"   value={info.matrix} />
          <StatBlock label="UPTIME"   value={info.uptime} />
          <StatBlock label="FIRMWARE" value={info.firmware} />
          <StatBlock label="STORAGE" value={`${info.storage.used} / ${info.storage.total} KB`} bar={info.storage.used / info.storage.total} />
        </div>
      </div>

      {/* List */}
      <SectionLabel>CONNECTION</SectionLabel>
      <Card>
        <Row label="BLE Device" detail={info.name} onClick={onOpenBle} chev />
        <Row label="MAC Address" detail={info.mac} mono />
        <Row label="Service UUID" detail="…ff00" mono last />
      </Card>

      <SectionLabel>PROGRAMS</SectionLabel>
      <Card>
        <Row label="Installed" detail={`${programCount} of 16`} chev />
        <Row label="Auto-start last program" toggle defaultOn />
        <Row label="Marketplace registry" detail="github" last chev />
      </Card>

      <SectionLabel>DEVICE</SectionLabel>
      <Card>
        <Row label="Restart" />
        <Row label="Wipe storage" danger />
        <Row label="Firmware OTA update" detail="up to date" last chev />
      </Card>

      <div style={{ padding: '20px 24px 40px', fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.35)', lineHeight: 1.6 }}>
        Serial {info.serial}<br />
        Built with wasm3 · LittleFS · NeoPixelBus
      </div>
    </div>
  );
}

function StatBlock({ label, value, bar }) {
  return (
    <div>
      <div style={{ fontSize: 10, letterSpacing: 1, color: 'rgba(250,250,247,0.4)', fontFamily: 'JetBrains Mono, monospace' }}>{label}</div>
      <div style={{ fontSize: 17, fontWeight: 700, marginTop: 4, letterSpacing: -0.3 }}>{value}</div>
      {bar !== undefined && (
        <div style={{ marginTop: 6, height: 3, background: 'rgba(255,255,255,0.08)', borderRadius: 2, overflow: 'hidden' }}>
          <div style={{ height: '100%', width: `${bar * 100}%`, background: '#60A5FA' }} />
        </div>
      )}
    </div>
  );
}

function SectionLabel({ children }) {
  return <div style={{ padding: '4px 28px 8px', fontSize: 11, fontFamily: 'JetBrains Mono, monospace', color: 'rgba(250,250,247,0.45)', letterSpacing: 1 }}>{children}</div>;
}

function Card({ children }) {
  return <div style={{ margin: '0 20px 20px', background: 'rgba(255,255,255,0.04)', border: '0.5px solid rgba(255,255,255,0.06)', borderRadius: 18, overflow: 'hidden' }}>{children}</div>;
}

function Row({ label, detail, mono, chev, toggle, defaultOn, danger, last, onClick }) {
  const [on, setOn] = React.useState(!!defaultOn);
  return (
    <div onClick={onClick} style={{
      display: 'flex', alignItems: 'center', padding: '14px 16px',
      borderBottom: last ? 0 : '0.5px solid rgba(255,255,255,0.06)',
      cursor: onClick || toggle ? 'pointer' : 'default',
      gap: 12,
    }}>
      <div style={{ flex: 1, fontSize: 14, color: danger ? '#F87171' : '#FAFAF7', fontWeight: danger ? 600 : 400 }}>{label}</div>
      {detail && <span style={{ fontSize: 13, color: 'rgba(250,250,247,0.55)', fontFamily: mono ? 'JetBrains Mono, monospace' : 'inherit' }}>{detail}</span>}
      {chev && <span style={{ color: 'rgba(250,250,247,0.4)' }}>{Icons.chevron}</span>}
      {toggle && <Toggle value={on} onChange={setOn} color="#FAFAF7" />}
    </div>
  );
}

window.BleScreen = BleScreen;
window.UploadScreen = UploadScreen;
window.SettingsScreen = SettingsScreen;
