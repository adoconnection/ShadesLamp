# App Improvements Spec v2

## 1. Library Screen

### 1.1 Covers not loading from lamp
**Problem**: After BLE connect, program covers are gray (fallback gradient).
**Root cause**: `GET_PROGRAMS` response only contains `id` + `name`. Cover data is in `/meta/{id}.json` on device.

**Solution — guid+version caching**:

1. **Firmware**: Extend `GET_PROGRAMS` response to include `guid` and `version` per program:
   ```json
   [{"id":0,"name":"Aurora","guid":"a1b2c3d4","version":"1.0.0"}, ...]
   ```
   This adds ~50 bytes per program — manageable even for 55 programs (~3KB total).

2. **App**: Maintain a meta cache in AsyncStorage keyed by `${guid}:${version}`:
   ```json
   {
     "a1b2c3d4:1.0.0": {"cover":{...},"pulse":"#06B6D4","author":"built-in","category":"Ambient","desc":"..."},
     "a1b2c3d4:1.1.0": {"cover":{...},"pulse":"#06B6D4","author":"built-in","category":"Ambient","desc":"..."}
   }
   ```
   This correctly handles multiple lamps with different versions of the same program.

3. **Connect flow**:
   - Call `getPrograms()` → get `[{id, name, guid, version}]`
   - For each program: check AsyncStorage cache by key `${guid}:${version}`
     - If key exists → use cached meta (no BLE call)
     - If key missing → call `getMeta(id)`, store in cache under `${guid}:${version}`
   - First connect: fetches all 55 metas (~5-10 seconds)
   - Subsequent connects: instant (0 GET_META calls if nothing changed)
   - Periodically prune stale cache entries (e.g. keep last 200)

4. **Firmware changes**:
   - `ProgramInfo` struct: add `guid` and `version` string fields
   - `ensureMetaLoaded()`: also extract guid+version from `/meta/{id}.json`
   - `getProgramListJson()`: include guid+version in output

**Files**:
- `firmware/program_manager.h` — guid/version in ProgramInfo
- `firmware/program_manager.cpp` — ensureMetaLoaded + getProgramListJson
- `app2/src/ble/commands.ts` — parse guid/version from getPrograms
- `app2/src/screens/BleConnectScreen.tsx` — cache logic
- `app2/src/store/useMetaCache.ts` — NEW: AsyncStorage meta cache helper

### 1.2 Remove params count from ProgramRow subtitle
**Current**: `{program.author} · {program.params.length} params · {program.size}`
**New**: `{program.author} · {program.category}`
File: `app2/src/components/ProgramRow.tsx`

### 1.3 Auto-reconnect on disconnect + app launch
- Store last connected device MAC in AsyncStorage (`@lastDeviceMac`)
- On app launch: if stored MAC exists, attempt connect automatically (show "Reconnecting..." in BleStatusPill)
- On disconnect during session: attempt reconnect every 5 seconds (3 attempts), then show "Disconnected" pill
- BleStatusPill text: tap to reconnect manually
- File: `app2/src/ble/manager.ts`, `app2/src/screens/BleConnectScreen.tsx`, `app2/src/store/useBleStore.ts`

### 1.4 Hero card animated overlays
**Reference**: handoff `library.jsx` lines 35-36
**Current**: Static `LinearGradient` only
**Add**: Two animated `Animated.View` overlays with radial-gradient-like effect using `react-native-reanimated`:
- Overlay 1: radial from 30%/20%, uses `cover.via || cover.from`, animated position shift
- Overlay 2: radial from 80%/80%, uses `cover.to`, animated position shift (different speed)
- Both with `mixBlendMode` equivalent (or opacity 0.3-0.5 for RN compatibility)
- Hero card shadow: `shadowColor: program.pulse, shadowOffset: {width:0, height:20}, shadowOpacity: 0.2, shadowRadius: 25`
File: `app2/src/screens/LibraryScreen.tsx`

### 1.5 Animated EQ bars on active program
**Current**: Static height bars in `ProgramRow`
**Add**: Animate bar heights with `withRepeat(withSequence(...))` in Reanimated. Each bar has different duration (0.5s-0.9s) and delay.
File: `app2/src/components/ProgramRow.tsx`

### 1.6 Drag-to-reorder programs
**UX**: Long-press triggers drag mode, visual feedback (scale up, shadow), drop to reorder
**Firmware**: Add `CMD_SET_ORDER` (0x29) — accepts array of program IDs in desired order. Stores in `/order.json`.
Add `CMD_GET_ORDER` (0x2A) — returns current order. If no custom order, returns IDs in natural order.
**App**: Use `react-native-draggable-flatlist` or manual gesture handler. On drop, send new order to firmware.
**Files**:
- `firmware/storage.h/.cpp` — `saveOrder()`, `loadOrder()`
- `firmware/program_manager.h/.cpp` — `setOrder()`, `getOrder()`, respect order in `getProgramListJson()`
- `firmware/ble_service.cpp` — CMD_SET_ORDER / CMD_GET_ORDER handlers
- `app2/src/ble/constants.ts`, `commands.ts` — new commands
- `app2/src/screens/LibraryScreen.tsx` — draggable list

---

## 2. Marketplace

### 2.1 Featured card — coverSvg not filling card
**Problem**: SVG cover doesn't stretch to fill the entire FeaturedCard area.
**Fix**: In `FeaturedCard`, ensure `SvgXml` uses `width="100%" height="100%"` with `preserveAspectRatio="xMidYMid slice"` to cover the entire card.
File: `app2/src/components/FeaturedCard.tsx`

### 2.2 Conditional rating/downloads display
**MarketRow + FeaturedCard**: Show rating and downloads only if present in catalog JSON.
Check `item.rating != null` and `item.downloads != null` before rendering.
If absent — hide those elements, don't show zeros or placeholders.
Files: `app2/src/components/MarketRow.tsx`, `app2/src/components/FeaturedCard.tsx`

### 2.3 Auto-activate after install
After successful install in `MarketDetailScreen`, automatically call `setActiveProgram(newId)` and update program store's `activeId`.
Show phase transition: `done` state shows "Installed & Running" instead of just "Installed".
The lamp starts running the new program immediately.
File: `app2/src/screens/MarketDetailScreen.tsx`

---

## 3. Market Detail Screen

### 3.1 Show installation status + Run button
**If not installed**: Show "Install" button (current behavior)
**If installed but not active**: Show "Run" button (green, calls `setActiveProgram`)
**If installed and active**: Show "Running" indicator (with EQ bars, non-clickable)
File: `app2/src/screens/MarketDetailScreen.tsx`

### 3.2 Add "Delete" button for installed programs
When program is installed, show red "Remove from device" button at bottom (same style as ProgramDetailScreen).
Calls `deleteProgram(id)` via BLE, updates market store.
File: `app2/src/screens/MarketDetailScreen.tsx`

### 3.3 Remove "Target" from Technical section
Delete the `<TechRow label="Target" value="wasm32-unknown" last />` line.
File: `app2/src/screens/MarketDetailScreen.tsx`

### 3.4 Hide [...] button
Remove `<NavButton icon={<MoreIcon />} />` from the nav bar — it has no functionality.
File: `app2/src/screens/MarketDetailScreen.tsx`

---

## 4. BLE Connect Screen

### 4.1 Auto-scan every 10 seconds
Replace single scan on mount with `setInterval` every 10 seconds. Keep scanning indicator active. Clear interval on unmount or on successful connect.
File: `app2/src/screens/BleConnectScreen.tsx`

### 4.2 Hide device list when empty
While `devices.length === 0`, don't render the NEARBY section or the list card. Show only the BLE animation and "Scanning..." label.
File: `app2/src/screens/BleConnectScreen.tsx`

### 4.3 Show short MAC instead of full UUID
**Current**: `d.id` shows full BLE UUID (e.g. `0000ff00-0000-1000-8000-00805f9b34fb`)
**Fix**: Extract last 17 chars or use `d.id` formatted as `XX:XX:XX:XX:XX:XX`. On Android, `device.id` is already a MAC. On iOS it's a UUID — show shortened form.
File: `app2/src/screens/BleConnectScreen.tsx`

### 4.4 Remove Service UUID text
Delete the `<Text style={styles.uuidLabel}>Service UUID ...</Text>` at bottom of screen.
File: `app2/src/screens/BleConnectScreen.tsx`

---

## 5. Device Settings Screen

### 5.1 Remove UPTIME stat
Remove `<StatBlock label="UPTIME" value={deviceInfo.uptime} />` from stats grid.
Remove `uptime` field from `DeviceInfo` type.
Files: `app2/src/screens/DeviceSettingsScreen.tsx`, `app2/src/types/ble.ts`

### 5.2 Free storage from firmware
**New firmware command**: `CMD_GET_STORAGE` (0x29 or next free) — returns `{"ok":true,"used":<bytes>,"total":<bytes>,"free":<bytes>}` using LittleFS `totalBytes()` / `usedBytes()`.
**App**: Call on connect, populate `deviceInfo.storage`.
**Files**:
- `firmware/ble_service.h/.cpp` — new command
- `firmware/storage.h/.cpp` — `getStorageInfo()` helper
- `app2/src/ble/constants.ts`, `commands.ts` — new command
- `app2/src/screens/BleConnectScreen.tsx` — call on connect

### 5.3 Header: show MAC instead of "ESP32-S3 · WasmLED"
**Current**: `<Text>ESP32-S3 · WasmLED</Text>`
**New**: `<Text>{shortMac(deviceInfo.mac)}</Text>` — show short MAC like `80:B5:4E:C5:06:8D`
File: `app2/src/screens/DeviceSettingsScreen.tsx`

### 5.4 Remove CONNECTION section
Delete entire `SectionLabel("CONNECTION")` + `Card` block (BLE Device, MAC Address, Service UUID rows).
File: `app2/src/screens/DeviceSettingsScreen.tsx`

### 5.5 Simplify PROGRAMS section
Keep only: `<SettingsRow label="Installed" detail="{count} of 128" />`
Remove: "Auto-start last program", "Marketplace registry" rows.
File: `app2/src/screens/DeviceSettingsScreen.tsx`

### 5.6 Simplify DEVICE section
Keep:
- `Restart` (existing)
- `Power` — toggle on/off (calls `setPower()` BLE command, syncs with `useBleStore.powerOn`)
Hide (keep in code, just don't render): "Wipe storage", "Firmware OTA update" rows.
File: `app2/src/screens/DeviceSettingsScreen.tsx`

### 5.7 Chip serial number
**Firmware**: Add `CMD_GET_SERIAL` or include in `CMD_GET_HW_CONFIG` response. Use `ESP.getEfuseMac()` to get unique 48-bit chip ID, format as hex string (e.g. `8CB54EC5068D`).
**App**: Display in footer as `Serial 8CB54EC5068D`.
**Files**:
- `firmware/ble_service.cpp` — include `serial` field in GET_HW_CONFIG response
- `app2/src/screens/BleConnectScreen.tsx` — parse and store
- `app2/src/screens/DeviceSettingsScreen.tsx` — display
- `app2/src/types/ble.ts` — update DeviceInfo type

---

## 6. General

### 6.1 All UI text in English
**Current**: BleStatusPill has `'Подключение…'` and `'Не подключено'`
**Fix**: Change to `'Connecting…'` and `'Not connected'`
File: `app2/src/components/BleStatusPill.tsx`

---

## Implementation Order

### Phase 1: Quick fixes (no firmware changes)
1. BleStatusPill: English text
2. ProgramRow: remove params count
3. MarketDetail: remove Target, hide [...] button
4. BleConnect: remove UUID label, hide empty list
5. DeviceSettings: remove uptime, connection section, simplify programs/device sections

### Phase 2: Firmware + BLE data flow
6. Firmware: guid+version in GET_PROGRAMS response
7. Firmware: serial number in GET_HW_CONFIG
8. Firmware: CMD_GET_STORAGE for free space
9. Firmware: CMD_SET_ORDER / CMD_GET_ORDER for drag-to-reorder
10. App: meta cache (AsyncStorage) + connect flow with guid/version diffing
11. App: auto-reconnect (AsyncStorage + retry logic)
12. BleConnect: auto-scan, short MAC

### Phase 3: Marketplace integration
12. MarketDetail: Run/Running button for installed programs
13. MarketDetail: Delete button
14. MarketDetail: auto-activate after install
15. FeaturedCard: fix SVG cover sizing

### Phase 4: Animations + UX polish
16. Library hero: animated overlays + glow shadow
17. ProgramRow: animated EQ bars
18. Drag-to-reorder (firmware CMD_SET_ORDER + app gesture)

---

## Files Summary

| File | Changes |
|------|---------|
| `firmware/ble_service.h` | CMD_GET_STORAGE, CMD_SET_ORDER, CMD_GET_ORDER |
| `firmware/ble_service.cpp` | New command handlers, serial in hw-config |
| `firmware/storage.h/.cpp` | getStorageInfo(), saveOrder/loadOrder |
| `firmware/program_manager.h/.cpp` | Order support (get/set) |
| `app2/src/components/BleStatusPill.tsx` | English text |
| `app2/src/components/ProgramRow.tsx` | Remove params count, animated EQ bars |
| `app2/src/components/Cover.tsx` | No changes needed |
| `app2/src/components/FeaturedCard.tsx` | Fix SVG sizing, conditional rating/downloads |
| `app2/src/components/MarketRow.tsx` | Conditional rating/downloads |
| `app2/src/screens/LibraryScreen.tsx` | Hero animation, glow, drag-to-reorder |
| `app2/src/screens/BleConnectScreen.tsx` | Auto-scan, hide empty, short MAC, auto-reconnect |
| `app2/src/screens/MarketDetailScreen.tsx` | Run button, delete, remove Target/[...] |
| `app2/src/screens/DeviceSettingsScreen.tsx` | Simplify sections, power toggle, MAC header |
| `app2/src/store/useBleStore.ts` | lastDeviceMac, reconnect logic |
| `app2/src/ble/manager.ts` | Auto-reconnect, disconnect handler |
| `app2/src/ble/constants.ts` | New command codes |
| `app2/src/ble/commands.ts` | New command functions |
| `app2/src/store/useMetaCache.ts` | NEW: AsyncStorage meta cache by guid+version |
| `app2/src/types/ble.ts` | Update DeviceInfo (remove uptime, add serial) |
