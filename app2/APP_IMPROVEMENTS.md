# App Improvements

## 1. BLE chunked meta transfer

Firmware now supports chunked meta upload/download via upload characteristic.

**CLI changes (done):**
- `set-meta` and `push-meta` send meta via UPLOAD_START(type=1) + chunks + UPLOAD_FINISH
- `get-meta` works via existing chunked sendResponse
- UPLOAD_FINISH timeout increased to 15s
- Inter-chunk delay added for reliable transfer

**Firmware changes (done):**
- `UPLOAD_START` accepts optional bytes: `type(1)` + `progId(1)` after size
- type=0 (default) = WASM upload, type=1 = META upload
- `UPLOAD_FINISH` branches on type: WASM saves program, META calls `setProgramMeta()`
- META upload doesn't pause rendering or show progress bar
- New fields in BleService: `uploadType`, `uploadMetaProgId`

**Files changed:**
- `firmware/ble_service.h` — uploadType, uploadMetaProgId fields
- `firmware/ble_service.cpp` — CMD_UPLOAD_START/FINISH type handling
- `client-cli/Program.cs` — UploadMeta(), chunked set-meta/push-meta

---

## 2. Library: meta cache from lamp

**Problem**: Program covers/descriptions on the Library screen require meta from the lamp. Currently meta is fetched each time from the catalog, not from the device.

**Solution**: Cache meta fetched from the lamp in AsyncStorage, keyed by `guid:version`.

**Flow:**
1. On connect, call `getPrograms()` → returns `[{id, name, guid, version}]`
2. For each program, check cache by `${guid}:${version}`
   - Cache hit → use cached meta (no BLE call)
   - Cache miss → call `getMeta(id)` via BLE, store result in cache
3. First connect: fetches all metas (uses chunked BLE transfer)
4. Subsequent connects: instant if nothing changed

**Files:**
- `app2/src/store/useMetaCache.ts` — NEW: AsyncStorage cache helper
- `app2/src/ble/commands.ts` — parse guid/version from getPrograms
- `app2/src/screens/BleConnectScreen.tsx` or connect flow — cache logic

---

## 3. Pull-to-refresh on Library and Marketplace

**Problem**: No way to manually refresh the program list or marketplace catalog.

**Solution**: Add pull-to-refresh gesture to both screens.

**Library screen:**
- Pull down on program list triggers re-fetch of all program data from lamp
- Invalidates meta cache for current device — re-fetches all metas via BLE
- Shows refresh indicator while loading

**Marketplace screen:**
- Pull down triggers re-fetch of catalog from GitHub/remote
- Refreshes featured list, categories, and program entries
- Shows refresh indicator while loading

**Implementation:**
- Use `RefreshControl` on `FlatList`/`ScrollView` in both screens
- Library: `onRefresh` calls `getPrograms()` + invalidates meta cache + re-fetches metas
- Marketplace: `onRefresh` re-fetches remote catalog JSON

**Files:**
- `app2/src/screens/LibraryScreen.tsx` — add RefreshControl
- `app2/src/screens/MarketplaceScreen.tsx` — add RefreshControl
- `app2/src/store/useMetaCache.ts` — add `invalidate()` method

---

## 4. Clear display on power off

**Problem**: When the lamp is turned off via the app, LEDs remain in their last state (or show residual glow).

**Solution**: When `setPower(false)` is called, clear all pixels before stopping rendering.

**Implementation:**
- In firmware `setPower(false)` handler: fill all LEDs with black (0,0,0), call `FastLED.show()`, then stop rendering
- Ensures clean off — no residual glow or frozen frame

**Files:**
- `firmware/led_driver.cpp` — clear strip in power-off logic
- `firmware/program_manager.cpp` — ensure render task stops after clearing

---

## 5. Remove "Remove program" button from Library detail

**Problem**: The program detail screen on the Library (main) tab shows a "Remove program" button. This is unwanted — program removal should only be available from a management/settings context, not the main screen.

**Solution**: Remove the "Remove program" button from the Library program detail screen.

**Files:**
- `app2/src/screens/ProgramDetailScreen.tsx` (or equivalent) — remove the delete/remove button and its handler

---

## 6. Add "Go to parameters" button on Marketplace detail

**Problem**: When viewing program details on the Marketplace screen for a program that is already installed on the lamp, there is no quick way to jump to its parameters/settings.

**Solution**: If the program is already installed, show a "Go to parameters" button that navigates to the program's parameter editing screen on the Library tab.

**Implementation:**
- Check if the marketplace program's guid matches any installed program on the lamp
- If installed: show "Go to parameters" button alongside (or instead of) the "Install" button
- Button navigates to Library tab → program detail → parameters screen

**Files:**
- `app2/src/screens/MarketplaceDetailScreen.tsx` (or equivalent) — add installed check + navigation button
- `app2/src/ble/commands.ts` or store — helper to check if program is installed by guid

---

## 7. Remove [...] menu button from Library detail header

**Problem**: The program detail screen on the Library tab has a [...] (more options) button in the top-right corner of the header. It's unnecessary.

**Solution**: Remove the [...] button from the header/navigation bar on the Library program detail screen.

**Files:**
- `app2/src/screens/ProgramDetailScreen.tsx` (or equivalent) — remove headerRight / more-options button

---

## 8. Alphabetical sorting and category filters on Library screen

**Problem**: Programs on the Library (main) screen are listed in the order they come from the lamp, with no sorting or filtering.

**Solution**:
- Sort programs alphabetically by name by default
- Add horizontal scrollable category filter chips (like on Marketplace) at the top of the list
- Categories come from program meta (e.g. "Nature", "Fire", "Abstract", etc.)
- "All" chip selected by default, shows all programs
- Selecting a category filters the list to only programs in that category

**Implementation:**
- Sort fetched program list by `name` (case-insensitive)
- Extract unique categories from program metas
- Render horizontal chip/pill row above the FlatList
- Filter list by selected category

**Files:**
- `app2/src/screens/LibraryScreen.tsx` — sorting + category filter UI
- Reuse category chip component from Marketplace if one exists

---

## 9. Disable controls when lamp is disconnected

**Problem**: When the lamp is not connected (BLE disconnected), UI controls (sliders, buttons, selects) on the program detail/parameters screen are still interactive. Tapping them does nothing or causes errors.

**Solution**:
- Don't hide controls — keep them visible so the user sees the current (cached) state
- Disable all interactive controls when BLE is disconnected (grayed out, non-interactive)
- Show a subtle indicator that the lamp is offline (e.g. banner or dimmed state)
- When the lamp reconnects: fetch fresh parameter values from the device and re-enable controls
- If values changed on the lamp side while disconnected, update the UI to reflect the new values

**Implementation:**
- Check BLE connection state in the parameters screen
- Pass `disabled` prop to all control components when disconnected
- On reconnect event: call `getParams()` for the active program, update local state
- Avoid sending stale cached values to the lamp on reconnect — always pull fresh first

**Files:**
- `app2/src/screens/ProgramDetailScreen.tsx` (or equivalent) — disable controls based on connection state
- `app2/src/ble/connectFlow.ts` or connection store — expose `isConnected` state
- Parameter control components — support `disabled` prop styling
