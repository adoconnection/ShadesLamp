using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using System.Text.Json;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

// ─── Enable Unicode output ──────────────────────────────────────────────────
Console.OutputEncoding = Encoding.UTF8;
Console.InputEncoding = Encoding.UTF8;

// ─── BLE UUIDs ───────────────────────────────────────────────────────────────

const string ServiceUuid = "0000ff00-0000-1000-8000-00805f9b34fb";
const string CharCommandUuid = "0000ff01-0000-1000-8000-00805f9b34fb";
const string CharResponseUuid = "0000ff02-0000-1000-8000-00805f9b34fb";
const string CharActiveProgramUuid = "0000ff03-0000-1000-8000-00805f9b34fb";
const string CharUploadUuid = "0000ff04-0000-1000-8000-00805f9b34fb";
const string CharParamValuesUuid = "0000ff05-0000-1000-8000-00805f9b34fb";

var serviceGuid = new Guid(ServiceUuid);
var commandGuid = new Guid(CharCommandUuid);
var responseGuid = new Guid(CharResponseUuid);
var activeProgramGuid = new Guid(CharActiveProgramUuid);
var uploadGuid = new Guid(CharUploadUuid);
var paramValuesGuid = new Guid(CharParamValuesUuid);

// ─── Command codes ───────────────────────────────────────────────────────────

const byte CMD_GET_PROGRAMS = 0x01;
const byte CMD_GET_PARAMS = 0x02;
const byte CMD_SET_PARAM = 0x03;
const byte CMD_GET_PARAM_VALUES = 0x04;
const byte CMD_UPLOAD_START = 0x10;
const byte CMD_UPLOAD_FINISH = 0x11;
const byte CMD_DELETE_PROGRAM = 0x12;
const byte CMD_SET_NAME = 0x20;
const byte CMD_GET_NAME = 0x21;

// ─── Chunked response flags ─────────────────────────────────────────────────

const byte FLAG_FINAL = 0x01;
const byte FLAG_ERROR = 0x02;

// ─── Upload chunk size ──────────────────────────────────────────────────────

const int UPLOAD_CHUNK_SIZE = 200;

// ─── Response timeout ───────────────────────────────────────────────────────

const int RESPONSE_TIMEOUT_MS = 5000;

// ─── TUI Constants ──────────────────────────────────────────────────────────

const int BOX_WIDTH = 50;
const int SLIDER_WIDTH = 20;

// ─── State ──────────────────────────────────────────────────────────────────

BluetoothLEDevice? connectedDevice = null;
GattDeviceService? connectedService = null;
GattCharacteristic? commandChar = null;
GattCharacteristic? responseChar = null;
GattCharacteristic? activeProgramChar = null;
GattCharacteristic? uploadChar = null;
GattCharacteristic? paramValuesChar = null;

var cts = new CancellationTokenSource();
var connectionReady = new TaskCompletionSource<bool>();

// Chunked response collection
var responseChunks = new SortedDictionary<byte, byte[]>();
TaskCompletionSource<string>? pendingResponse = null;
object responseLock = new();

// Chunked param values notify collection
var paramValuesChunks = new SortedDictionary<byte, byte[]>();
object paramValuesLock = new();
// Shared param values cache for the parameters menu (updated by ff05 notify)
Dictionary<int, JsonElement>? liveParamValues = null;
bool paramValuesUpdated = false;

// Program state
var programs = new List<ProgramInfo>();
int activeProgram = -1;
string deviceName = "Unknown";
string deviceAddress = "";

// TUI state
int menuCursor = 0;
string statusMessage = "";
ConsoleColor statusColor = ConsoleColor.Gray;
bool needsRedraw = true;

// ─── Ctrl+C / Process exit ──────────────────────────────────────────────────

Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    cts.Cancel();
};

AppDomain.CurrentDomain.ProcessExit += (_, _) =>
{
    connectedService?.Dispose();
    connectedDevice?.Dispose();
};

// ─── BLE Scanner ────────────────────────────────────────────────────────────

var watcher = new BluetoothLEAdvertisementWatcher
{
    ScanningMode = BluetoothLEScanningMode.Active
};
watcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(serviceGuid);

var discoveredDevices = new HashSet<ulong>();

// Scan results for device selection screen
var scanResults = new List<(ulong Address, string Name)>();
object scanLock = new();
bool scanMode = false; // true = collecting devices for selection screen

watcher.Received += async (sender, args) =>
{
    if (scanMode)
    {
        // Collecting devices for selection screen
        lock (scanLock)
        {
            if (!scanResults.Any(d => d.Address == args.BluetoothAddress))
            {
                var name = string.IsNullOrEmpty(args.Advertisement.LocalName)
                    ? "Shades LED Lamp"
                    : args.Advertisement.LocalName;
                scanResults.Add((args.BluetoothAddress, name));
            }
        }
        return;
    }

    // Reconnection mode — connect to first found
    if (!discoveredDevices.Add(args.BluetoothAddress))
        return;

    var devName = string.IsNullOrEmpty(args.Advertisement.LocalName)
        ? "Shades LED Lamp"
        : args.Advertisement.LocalName;

    watcher.Stop();

    try
    {
        await ConnectToDevice(args.BluetoothAddress, devName);
    }
    catch (Exception)
    {
        discoveredDevices.Clear();
        try { watcher.Start(); } catch { }
    }
};

watcher.Stopped += (sender, args) => { };

// ─── Scan for devices (returns list) ────────────────────────────────────────

async Task<List<(ulong Address, string Name)>> ScanForDevices(int durationMs = 3000)
{
    lock (scanLock)
    {
        scanResults.Clear();
        scanMode = true;
    }

    watcher.Start();
    await Task.Delay(durationMs);
    watcher.Stop();

    lock (scanLock)
    {
        scanMode = false;
        return new List<(ulong Address, string Name)>(scanResults);
    }
}

// ─── Device Selection Screen ────────────────────────────────────────────────

void DrawDeviceSelectionScreen(List<(ulong Address, string Name)> devices, int cursor, bool scanning)
{
    Console.Clear();
    Console.CursorVisible = false;
    Console.ForegroundColor = ConsoleColor.Gray;

    DrawBorderTop();
    DrawLineCustom(() =>
    {
        WriteColored("WasmLED Controller", ConsoleColor.White);
    }, 18);
    DrawBorderMid();

    if (scanning)
    {
        DrawEmptyLine();
        DrawLineCustom(() =>
        {
            WriteColored("Scanning for devices...", ConsoleColor.Yellow);
        }, 22);
        DrawEmptyLine();
        DrawBorderBot();
        return;
    }

    DrawLineCustom(() =>
    {
        WriteColored("Found devices:", ConsoleColor.White);
    }, 14);
    DrawEmptyLine();

    if (devices.Count == 0)
    {
        DrawLineCustom(() =>
        {
            WriteColored("(no devices found)", ConsoleColor.DarkGray);
        }, 18);
    }
    else
    {
        for (int i = 0; i < devices.Count; i++)
        {
            var dev = devices[i];
            bool isSel = i == cursor;
            var arrow = isSel ? "\u25ba " : "  ";
            var addr = FormatBluetoothAddress(dev.Address);
            var name = dev.Name;

            int maxName = BOX_WIDTH - 2 - 4 - arrow.Length - addr.Length - 2;
            if (name.Length > maxName) name = name[..maxName];
            int lineLen = arrow.Length + name.Length + 2 + addr.Length;

            DrawLineCustom(() =>
            {
                if (isSel)
                {
                    WriteColored(arrow, ConsoleColor.Cyan);
                    WriteColoredBg(name, ConsoleColor.White, ConsoleColor.DarkBlue);
                    Console.BackgroundColor = ConsoleColor.Black;
                }
                else
                {
                    Console.Write(arrow);
                    WriteColored(name, ConsoleColor.Gray);
                }
                Console.Write("  ");
                WriteColored(addr, ConsoleColor.DarkGray);
            }, lineLen);
        }
    }

    DrawEmptyLine();
    DrawBorderMid();

    DrawLineCustom(() =>
    {
        WriteColored("\u2191\u2193", ConsoleColor.White);
        WriteColored(" Select   ", ConsoleColor.Gray);
        WriteColored("Enter", ConsoleColor.White);
        WriteColored(" Connect   ", ConsoleColor.Gray);
        WriteColored("R", ConsoleColor.White);
        WriteColored(" Rescan", ConsoleColor.Gray);
    }, 33);

    DrawLineCustom(() =>
    {
        WriteColored("Q", ConsoleColor.White);
        WriteColored("  Quit", ConsoleColor.Gray);
    }, 7);

    DrawBorderBot();
}

async Task<(ulong Address, string Name)?> ShowDeviceSelectionScreen()
{
    DrawDeviceSelectionScreen(new(), 0, scanning: true);
    var devices = await ScanForDevices();

    // Auto-connect if exactly one device found
    if (devices.Count == 1)
        return devices[0];

    int cursor = 0;
    DrawDeviceSelectionScreen(devices, cursor, scanning: false);

    while (!cts.IsCancellationRequested)
    {
        if (!Console.KeyAvailable)
        {
            await Task.Delay(50);
            continue;
        }

        var key = Console.ReadKey(true);
        switch (key.Key)
        {
            case ConsoleKey.Q:
                return null;

            case ConsoleKey.UpArrow:
                if (devices.Count > 0 && cursor > 0)
                {
                    cursor--;
                    DrawDeviceSelectionScreen(devices, cursor, scanning: false);
                }
                break;

            case ConsoleKey.DownArrow:
                if (devices.Count > 0 && cursor < devices.Count - 1)
                {
                    cursor++;
                    DrawDeviceSelectionScreen(devices, cursor, scanning: false);
                }
                break;

            case ConsoleKey.Enter:
                if (devices.Count > 0 && cursor >= 0 && cursor < devices.Count)
                    return devices[cursor];
                break;

            case ConsoleKey.R:
                cursor = 0;
                DrawDeviceSelectionScreen(new(), 0, scanning: true);
                devices = await ScanForDevices();
                if (devices.Count == 1)
                    return devices[0];
                DrawDeviceSelectionScreen(devices, cursor, scanning: false);
                break;
        }
    }

    return null;
}

// ─── Connect to device ──────────────────────────────────────────────────────

async Task ConnectToDevice(ulong bluetoothAddress, string name)
{
    connectedDevice = await BluetoothLEDevice.FromBluetoothAddressAsync(bluetoothAddress);

    if (connectedDevice == null)
    {
        discoveredDevices.Clear();
        watcher.Start();
        return;
    }

    deviceName = name;
    deviceAddress = FormatBluetoothAddress(bluetoothAddress);

    connectedDevice.ConnectionStatusChanged += (dev, _) =>
    {
        if (dev.ConnectionStatus == BluetoothConnectionStatus.Disconnected)
        {
            commandChar = null;
            responseChar = null;
            activeProgramChar = null;
            uploadChar = null;
            paramValuesChar = null;
            connectedService?.Dispose();
            connectedService = null;
            connectedDevice?.Dispose();
            connectedDevice = null;
            discoveredDevices.Clear();

            // Cancel any pending response
            lock (responseLock)
            {
                pendingResponse?.TrySetException(new IOException("Device disconnected"));
                pendingResponse = null;
                responseChunks.Clear();
            }

            // Reset connection ready for re-connection
            connectionReady = new TaskCompletionSource<bool>();

            try { watcher.Start(); } catch { }
        }
    };

    // Service discovery with retries
    GattDeviceService? service = null;
    for (int attempt = 1; attempt <= 5; attempt++)
    {
        await Task.Delay(2000);

        try
        {
            var allServices = await connectedDevice.GetGattServicesAsync(BluetoothCacheMode.Uncached);
            if (allServices.Status != GattCommunicationStatus.Success)
                continue;

            foreach (var s in allServices.Services)
            {
                if (s.Uuid == serviceGuid)
                    service = s;
                else
                    s.Dispose();
            }

            if (service != null) break;
        }
        catch (Exception)
        {
        }
    }

    if (service == null)
    {
        connectedDevice.Dispose();
        connectedDevice = null;
        discoveredDevices.Clear();
        watcher.Start();
        return;
    }

    connectedService = service;

    // Discover all characteristics
    var charsResult = await service.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
    if (charsResult.Status != GattCommunicationStatus.Success)
    {
        service.Dispose();
        connectedDevice.Dispose();
        connectedDevice = null;
        discoveredDevices.Clear();
        watcher.Start();
        return;
    }

    foreach (var c in charsResult.Characteristics)
    {
        if (c.Uuid == commandGuid) commandChar = c;
        else if (c.Uuid == responseGuid) responseChar = c;
        else if (c.Uuid == activeProgramGuid) activeProgramChar = c;
        else if (c.Uuid == uploadGuid) uploadChar = c;
        else if (c.Uuid == paramValuesGuid) paramValuesChar = c;
    }

    // Verify we have the required characteristics
    if (commandChar == null || responseChar == null || activeProgramChar == null || uploadChar == null)
    {
        service.Dispose();
        connectedDevice.Dispose();
        connectedDevice = null;
        discoveredDevices.Clear();
        watcher.Start();
        return;
    }

    // Subscribe to Response (ff02) notifications
    responseChar.ValueChanged += OnResponseNotification;
    var notifyStatus = await responseChar.WriteClientCharacteristicConfigurationDescriptorAsync(
        GattClientCharacteristicConfigurationDescriptorValue.Notify);

    if (notifyStatus != GattCommunicationStatus.Success)
    {
        service.Dispose();
        connectedDevice.Dispose();
        connectedDevice = null;
        discoveredDevices.Clear();
        watcher.Start();
        return;
    }

    // Subscribe to Active Program (ff03) notifications
    activeProgramChar.ValueChanged += OnActiveProgramNotification;
    var activeProgramNotifyStatus = await activeProgramChar.WriteClientCharacteristicConfigurationDescriptorAsync(
        GattClientCharacteristicConfigurationDescriptorValue.Notify);

    // Subscribe to Param Values (ff05) notifications (optional — older firmware may lack it)
    if (paramValuesChar != null)
    {
        paramValuesChar.ValueChanged += OnParamValuesNotification;
        await paramValuesChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
    }

    // Read current active program
    try
    {
        var readResult = await activeProgramChar.ReadValueAsync(BluetoothCacheMode.Uncached);
        if (readResult.Status == GattCommunicationStatus.Success)
        {
            var data = readResult.Value.ToArray();
            if (data.Length >= 1)
                activeProgram = data[0];
        }
    }
    catch (Exception)
    {
    }

    connectionReady.TrySetResult(true);
}

// ─── Response notification handler (chunked) ────────────────────────────────

void OnResponseNotification(GattCharacteristic sender, GattValueChangedEventArgs args)
{
    var data = args.CharacteristicValue.ToArray();
    if (data.Length < 2) return;

    byte seq = data[0];
    byte flags = data[1];
    byte[] payload = data.Length > 2 ? data[2..] : Array.Empty<byte>();

    lock (responseLock)
    {
        responseChunks[seq] = payload;

        bool isFinal = (flags & FLAG_FINAL) != 0;
        bool isError = (flags & FLAG_ERROR) != 0;

        if (isFinal)
        {
            // Assemble full response from all chunks
            using var ms = new MemoryStream();
            foreach (var kvp in responseChunks)
                ms.Write(kvp.Value, 0, kvp.Value.Length);

            responseChunks.Clear();

            var json = Encoding.UTF8.GetString(ms.ToArray());

            if (isError)
            {
                pendingResponse?.TrySetException(new BleCommandException(json));
            }
            else
            {
                pendingResponse?.TrySetResult(json);
            }
        }
    }
}

// ─── Active Program notification handler ────────────────────────────────────

void OnActiveProgramNotification(GattCharacteristic sender, GattValueChangedEventArgs args)
{
    var data = args.CharacteristicValue.ToArray();
    if (data.Length >= 1)
    {
        activeProgram = data[0];
    }
}

// ─── Param Values notification handler (chunked, same format as response) ────

void OnParamValuesNotification(GattCharacteristic sender, GattValueChangedEventArgs args)
{
    var data = args.CharacteristicValue.ToArray();
    if (data.Length < 2) return;

    byte seq = data[0];
    byte flags = data[1];
    byte[] payload = data.Length > 2 ? data[2..] : Array.Empty<byte>();

    lock (paramValuesLock)
    {
        paramValuesChunks[seq] = payload;

        bool isFinal = (flags & FLAG_FINAL) != 0;
        if (isFinal)
        {
            using var ms = new MemoryStream();
            foreach (var kvp in paramValuesChunks)
                ms.Write(kvp.Value, 0, kvp.Value.Length);
            paramValuesChunks.Clear();

            var json = Encoding.UTF8.GetString(ms.ToArray());
            try
            {
                using var doc = JsonDocument.Parse(json);
                var newValues = new Dictionary<int, JsonElement>();
                foreach (var prop in doc.RootElement.EnumerateObject())
                {
                    if (int.TryParse(prop.Name, out var paramId))
                        newValues[paramId] = prop.Value.Clone();
                }
                liveParamValues = newValues;
                paramValuesUpdated = true;
            }
            catch { }
        }
    }
}

// ─── Send BLE command and await chunked response ────────────────────────────

async Task<string> SendCommand(byte commandCode, byte[]? payload = null)
{
    if (commandChar == null)
        throw new InvalidOperationException("Not connected");

    // Prepare the pending response before writing
    TaskCompletionSource<string> tcs;
    lock (responseLock)
    {
        responseChunks.Clear();
        tcs = new TaskCompletionSource<string>();
        pendingResponse = tcs;
    }

    // Build command data
    int totalLen = 1 + (payload?.Length ?? 0);
    var writer = new DataWriter();
    writer.WriteByte(commandCode);
    if (payload != null)
        writer.WriteBytes(payload);

    var writeResult = await commandChar.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
    if (writeResult != GattCommunicationStatus.Success)
    {
        lock (responseLock)
        {
            pendingResponse = null;
            responseChunks.Clear();
        }
        throw new IOException($"BLE write failed: {writeResult}");
    }

    // Wait for response with timeout
    using var timeoutCts = new CancellationTokenSource(RESPONSE_TIMEOUT_MS);
    using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(timeoutCts.Token, cts.Token);

    try
    {
        var completedTask = await Task.WhenAny(tcs.Task, Task.Delay(Timeout.Infinite, linkedCts.Token));
        if (completedTask == tcs.Task)
            return await tcs.Task;

        throw new TimeoutException("Response timeout (5s)");
    }
    catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested)
    {
        throw new TimeoutException("Response timeout (5s)");
    }
    finally
    {
        lock (responseLock)
        {
            if (pendingResponse == tcs)
            {
                pendingResponse = null;
                responseChunks.Clear();
            }
        }
    }
}

// ─── Write to Active Program characteristic ─────────────────────────────────

async Task SetActiveProgram(byte programId)
{
    if (activeProgramChar == null)
        throw new InvalidOperationException("Not connected");

    var writer = new DataWriter();
    writer.WriteByte(programId);

    var result = await activeProgramChar.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
    if (result != GattCommunicationStatus.Success)
        throw new IOException($"Failed to set active program: {result}");

    activeProgram = programId;
}

// ─── Fetch program list ─────────────────────────────────────────────────────

async Task RefreshPrograms()
{
    var json = await SendCommand(CMD_GET_PROGRAMS);
    programs.Clear();

    using var doc = JsonDocument.Parse(json);
    foreach (var elem in doc.RootElement.EnumerateArray())
    {
        var id = elem.GetProperty("id").GetInt32();
        var name = elem.GetProperty("name").GetString() ?? $"Program {id}";
        programs.Add(new ProgramInfo(id, name));
    }
}

// ─── Upload WASM file (with TUI progress) ───────────────────────────────────

async Task<int> UploadWasm(string filePath, int progressRow)
{
    if (uploadChar == null || commandChar == null)
        throw new InvalidOperationException("Not connected");

    var fileData = await File.ReadAllBytesAsync(filePath);

    // 1. UPLOAD_START with file size
    var sizeBytes = BitConverter.GetBytes((uint)fileData.Length);
    var startResponse = await SendCommand(CMD_UPLOAD_START, sizeBytes);

    using (var doc = JsonDocument.Parse(startResponse))
    {
        if (!doc.RootElement.TryGetProperty("ok", out var okElem) || !okElem.GetBoolean())
        {
            var err = doc.RootElement.TryGetProperty("err", out var errElem)
                ? errElem.GetString() ?? "Unknown error"
                : "Unknown error";
            throw new BleCommandException($"Upload start failed: {err}");
        }
    }

    // 2. Send chunks via Upload characteristic (WRITE_NR)
    int offset = 0;
    int chunkIndex = 0;
    int totalChunks = (fileData.Length + UPLOAD_CHUNK_SIZE - 1) / UPLOAD_CHUNK_SIZE;

    while (offset < fileData.Length)
    {
        int chunkLen = Math.Min(UPLOAD_CHUNK_SIZE, fileData.Length - offset);
        var writer = new DataWriter();
        writer.WriteBytes(fileData.AsSpan(offset, chunkLen).ToArray());

        var writeResult = await uploadChar.WriteValueAsync(
            writer.DetachBuffer(), GattWriteOption.WriteWithoutResponse);

        if (writeResult != GattCommunicationStatus.Success)
            throw new IOException($"Upload chunk write failed at offset {offset}: {writeResult}");

        offset += chunkLen;
        chunkIndex++;

        // Draw progress bar
        int percent = (int)((long)offset * 100 / fileData.Length);
        DrawUploadProgress(progressRow, percent, chunkIndex, totalChunks, fileData.Length);

        // Small delay between chunks to avoid overwhelming the BLE stack
        if (chunkIndex % 10 == 0)
            await Task.Delay(20);
    }

    // 3. UPLOAD_FINISH
    var finishResponse = await SendCommand(CMD_UPLOAD_FINISH);

    using var finishDoc = JsonDocument.Parse(finishResponse);
    if (finishDoc.RootElement.TryGetProperty("ok", out var finishOk) && finishOk.GetBoolean())
    {
        if (finishDoc.RootElement.TryGetProperty("id", out var idElem))
            return idElem.GetInt32();
        return -1;
    }
    else
    {
        var err = finishDoc.RootElement.TryGetProperty("err", out var errElem)
            ? errElem.GetString() ?? "Unknown error"
            : "Unknown error";
        throw new BleCommandException($"Upload finish failed: {err}");
    }
}

// ─── TUI Drawing Helpers ────────────────────────────────────────────────────

void SetStatus(string message, ConsoleColor color)
{
    statusMessage = message;
    statusColor = color;
}

void WriteColored(string text, ConsoleColor fg)
{
    var prev = Console.ForegroundColor;
    Console.ForegroundColor = fg;
    Console.Write(text);
    Console.ForegroundColor = prev;
}

void WriteColoredBg(string text, ConsoleColor fg, ConsoleColor bg)
{
    var prevFg = Console.ForegroundColor;
    var prevBg = Console.BackgroundColor;
    Console.ForegroundColor = fg;
    Console.BackgroundColor = bg;
    Console.Write(text);
    Console.ForegroundColor = prevFg;
    Console.BackgroundColor = prevBg;
}

void DrawBorderTop()
{
    WriteColored("\u2554" + new string('\u2550', BOX_WIDTH - 2) + "\u2557", ConsoleColor.DarkCyan);
    Console.WriteLine();
}

void DrawBorderMid()
{
    WriteColored("\u2560" + new string('\u2550', BOX_WIDTH - 2) + "\u2563", ConsoleColor.DarkCyan);
    Console.WriteLine();
}

void DrawBorderBot()
{
    WriteColored("\u255a" + new string('\u2550', BOX_WIDTH - 2) + "\u255d", ConsoleColor.DarkCyan);
    Console.WriteLine();
}

// Draw a line where we handle coloring inline - takes action that writes content
// and the plain text length of that content for padding calculation
void DrawLineCustom(Action writeContent, int contentLength)
{
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.Write("  ");
    writeContent();
    int pad = BOX_WIDTH - 2 - contentLength - 2;
    if (pad > 0) Console.Write(new string(' ', pad));
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.WriteLine();
}

void DrawEmptyLine()
{
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.Write(new string(' ', BOX_WIDTH - 2));
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.WriteLine();
}

// ─── Scanning Screen ────────────────────────────────────────────────────────

void DrawScanningScreen()
{
    Console.Clear();
    Console.CursorVisible = false;
    Console.ForegroundColor = ConsoleColor.Gray;

    DrawBorderTop();
    DrawLineCustom(() =>
    {
        WriteColored("WasmLED Controller", ConsoleColor.White);
    }, 18);
    DrawBorderMid();
    DrawEmptyLine();
    DrawLineCustom(() =>
    {
        WriteColored("Scanning for WasmLED device...", ConsoleColor.Yellow);
    }, 30);
    DrawLineCustom(() =>
    {
        WriteColored("o o o", ConsoleColor.DarkGray);
    }, 5);
    DrawEmptyLine();
    DrawBorderBot();
}

// ─── Main Menu Drawing ──────────────────────────────────────────────────────

void DrawMainMenuScreen()
{
    Console.Clear();
    Console.CursorVisible = false;
    Console.ForegroundColor = ConsoleColor.Gray;

    // Title
    DrawBorderTop();
    DrawLineCustom(() =>
    {
        WriteColored("WasmLED Controller", ConsoleColor.White);
    }, 18);

    // Connection info
    var connStr = $"Connected to: {deviceName}";
    if (connStr.Length > BOX_WIDTH - 6)
        connStr = connStr[..(BOX_WIDTH - 6)];
    DrawLineCustom(() =>
    {
        WriteColored(connStr, ConsoleColor.DarkGray);
    }, connStr.Length);

    DrawBorderMid();

    // Programs header
    DrawLineCustom(() =>
    {
        WriteColored("Programs:", ConsoleColor.White);
    }, 9);

    DrawEmptyLine();

    if (programs.Count == 0)
    {
        DrawLineCustom(() =>
        {
            WriteColored("(no programs loaded)", ConsoleColor.DarkGray);
        }, 20);
    }
    else
    {
        for (int i = 0; i < programs.Count; i++)
        {
            var p = programs[i];
            bool isSelected = i == menuCursor;
            bool isActive = p.Id == activeProgram;

            var arrow = isSelected ? "\u25ba " : "  ";
            var name = p.Name;
            var tag = isActive ? " [ACTIVE]" : "";

            // Truncate if too long
            int maxNameLen = BOX_WIDTH - 2 - 4 - arrow.Length - tag.Length;
            if (name.Length > maxNameLen)
                name = name[..maxNameLen];

            int lineLen = arrow.Length + name.Length + tag.Length;

            DrawLineCustom(() =>
            {
                if (isSelected)
                {
                    WriteColored(arrow, ConsoleColor.Cyan);
                    WriteColoredBg(name, ConsoleColor.White, ConsoleColor.DarkBlue);
                    Console.BackgroundColor = ConsoleColor.Black;
                }
                else
                {
                    Console.Write(arrow);
                    WriteColored(name, ConsoleColor.Gray);
                }
                if (isActive)
                {
                    WriteColored(tag, ConsoleColor.Green);
                }
            }, lineLen);
        }
    }

    DrawEmptyLine();
    DrawBorderMid();

    // Help
    DrawLineCustom(() =>
    {
        WriteColored("\u2191\u2193", ConsoleColor.White);
        WriteColored(" Navigate  ", ConsoleColor.Gray);
        WriteColored("Enter", ConsoleColor.White);
        WriteColored(" Select", ConsoleColor.Gray);
    }, 24);

    DrawLineCustom(() =>
    {
        WriteColored("P", ConsoleColor.White);
        WriteColored("  Parameters  ", ConsoleColor.Gray);
        WriteColored("U", ConsoleColor.White);
        WriteColored(" Upload  ", ConsoleColor.Gray);
        WriteColored("D", ConsoleColor.White);
        WriteColored(" Delete", ConsoleColor.Gray);
    }, 31);

    DrawLineCustom(() =>
    {
        WriteColored("N", ConsoleColor.White);
        WriteColored("  Rename device   ", ConsoleColor.Gray);
        WriteColored("Q", ConsoleColor.White);
        WriteColored(" Quit", ConsoleColor.Gray);
    }, 24);

    DrawBorderBot();

    // Status line
    if (!string.IsNullOrEmpty(statusMessage))
    {
        WriteColored("  " + statusMessage, statusColor);
        Console.WriteLine();
    }
}

// ─── Parameters Menu Drawing & Logic ────────────────────────────────────────

string BuildSlider(float fraction, int width)
{
    fraction = Math.Clamp(fraction, 0f, 1f);
    int pos = (int)(fraction * (width - 1));
    var sb = new StringBuilder();
    sb.Append('[');
    for (int i = 0; i < width; i++)
    {
        if (i == pos)
            sb.Append('\u25cf'); // filled circle for knob
        else
            sb.Append('=');
    }
    sb.Append(']');
    return sb.ToString();
}

void DrawSliderColored(float fraction, int width)
{
    fraction = Math.Clamp(fraction, 0f, 1f);
    int pos = (int)(fraction * (width - 1));
    WriteColored("[", ConsoleColor.DarkGray);
    for (int i = 0; i < width; i++)
    {
        if (i == pos)
        {
            WriteColored("\u25cf", ConsoleColor.White);
        }
        else if (i < pos)
        {
            WriteColored("=", ConsoleColor.Cyan);
        }
        else
        {
            WriteColored("=", ConsoleColor.DarkGray);
        }
    }
    WriteColored("]", ConsoleColor.DarkGray);
}

async Task ShowParametersMenuTUI()
{
    if (activeProgram < 0)
    {
        SetStatus("No active program selected", ConsoleColor.Yellow);
        return;
    }

    var activeProgramInfo = programs.Find(p => p.Id == activeProgram);
    var programName = activeProgramInfo?.Name ?? $"Program {activeProgram}";

    // Fetch parameter metadata
    string paramsJson;
    try
    {
        paramsJson = await SendCommand(CMD_GET_PARAMS, new byte[] { (byte)activeProgram });
    }
    catch (Exception ex)
    {
        SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
        return;
    }

    // Fetch current parameter values
    string valuesJson;
    try
    {
        valuesJson = await SendCommand(CMD_GET_PARAM_VALUES, new byte[] { (byte)activeProgram });
    }
    catch (Exception ex)
    {
        SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
        return;
    }

    // Parse parameters metadata
    var paramsList = new List<ParamMeta>();
    using (var paramsDoc = JsonDocument.Parse(paramsJson))
    {
        foreach (var elem in paramsDoc.RootElement.EnumerateArray())
        {
            var pm = new ParamMeta
            {
                Id = elem.GetProperty("id").GetInt32(),
                Name = elem.GetProperty("name").GetString() ?? "?",
                Type = elem.GetProperty("type").GetString() ?? "int"
            };

            if (elem.TryGetProperty("min", out var minElem))
            {
                if (pm.Type == "float")
                    pm.MinF = minElem.GetSingle();
                else
                    pm.Min = minElem.GetInt32();
            }

            if (elem.TryGetProperty("max", out var maxElem))
            {
                if (pm.Type == "float")
                    pm.MaxF = maxElem.GetSingle();
                else
                    pm.Max = maxElem.GetInt32();
            }

            if (elem.TryGetProperty("options", out var optionsElem))
            {
                pm.Options = new List<string>();
                foreach (var opt in optionsElem.EnumerateArray())
                    pm.Options.Add(opt.GetString() ?? "?");
            }

            paramsList.Add(pm);
        }
    }

    // Parse current values
    var currentValues = new Dictionary<int, JsonElement>();
    using (var valuesDoc = JsonDocument.Parse(valuesJson))
    {
        foreach (var prop in valuesDoc.RootElement.EnumerateObject())
        {
            if (int.TryParse(prop.Name, out var paramId))
                currentValues[paramId] = prop.Value.Clone();
        }
    }

    if (paramsList.Count == 0)
    {
        SetStatus("No parameters for this program", ConsoleColor.Yellow);
        return;
    }

    int paramCursor = 0;
    string paramStatus = "";
    ConsoleColor paramStatusColor = ConsoleColor.Gray;
    bool paramNeedsRedraw = true;

    while (!cts.IsCancellationRequested)
    {
        if (paramNeedsRedraw)
        {
            Console.Clear();
            Console.CursorVisible = false;
            Console.ForegroundColor = ConsoleColor.Gray;

            DrawBorderTop();
            DrawLineCustom(() =>
            {
                WriteColored($"{programName} - Parameters", ConsoleColor.White);
            }, programName.Length + 13);
            DrawBorderMid();
            DrawEmptyLine();

            for (int i = 0; i < paramsList.Count; i++)
            {
                var pm = paramsList[i];
                bool isSel = i == paramCursor;
                var arrow = isSel ? "\u25ba " : "  ";

                string valueStr = "?";
                float fraction = 0f;

                if (currentValues.TryGetValue(pm.Id, out var val))
                {
                    switch (pm.Type)
                    {
                        case "float":
                            var fv = val.GetSingle();
                            valueStr = fv.ToString("F2");
                            if (pm.MaxF != pm.MinF)
                                fraction = (fv - pm.MinF) / (pm.MaxF - pm.MinF);
                            break;
                        case "bool":
                            var bv = val.GetInt32();
                            valueStr = bv != 0 ? "ON" : "OFF";
                            fraction = bv != 0 ? 1f : 0f;
                            break;
                        case "select":
                            var sv = val.GetInt32();
                            valueStr = pm.Options != null && sv >= 0 && sv < pm.Options.Count
                                ? pm.Options[sv] : sv.ToString();
                            if (pm.Options != null && pm.Options.Count > 1)
                                fraction = (float)sv / (pm.Options.Count - 1);
                            break;
                        default: // int
                            var iv = val.GetInt32();
                            valueStr = iv.ToString();
                            if (pm.Max != pm.Min)
                                fraction = (float)(iv - pm.Min) / (pm.Max - pm.Min);
                            break;
                    }
                }

                // Truncate name
                var displayName = pm.Name;
                if (displayName.Length > 14)
                    displayName = displayName[..14];

                // Calculate lengths for padding
                // arrow(2) + name(padded to 14) + " " + slider(SLIDER_WIDTH+2) + " " + value
                string sliderText = BuildSlider(fraction, SLIDER_WIDTH);
                int lineLen = arrow.Length + 14 + 1 + sliderText.Length + 1 + valueStr.Length;

                DrawLineCustom(() =>
                {
                    if (isSel)
                    {
                        WriteColored(arrow, ConsoleColor.Cyan);
                        WriteColoredBg(displayName.PadRight(14), ConsoleColor.White, ConsoleColor.DarkBlue);
                        Console.BackgroundColor = ConsoleColor.Black;
                    }
                    else
                    {
                        Console.Write(arrow);
                        WriteColored(displayName.PadRight(14), ConsoleColor.Gray);
                    }
                    Console.Write(" ");
                    DrawSliderColored(fraction, SLIDER_WIDTH);
                    Console.Write(" ");
                    WriteColored(valueStr, ConsoleColor.White);
                }, lineLen);
            }

            DrawEmptyLine();
            DrawBorderMid();

            DrawLineCustom(() =>
            {
                WriteColored("\u2191\u2193", ConsoleColor.White);
                WriteColored(" Navigate   ", ConsoleColor.Gray);
                WriteColored("\u2190\u2192", ConsoleColor.White);
                WriteColored(" Adjust value", ConsoleColor.Gray);
            }, 27);

            DrawLineCustom(() =>
            {
                WriteColored("Enter", ConsoleColor.White);
                WriteColored("  Type value   ", ConsoleColor.Gray);
                WriteColored("Esc", ConsoleColor.White);
                WriteColored("  Back", ConsoleColor.Gray);
            }, 24);

            DrawBorderBot();

            if (!string.IsNullOrEmpty(paramStatus))
            {
                WriteColored("  " + paramStatus, paramStatusColor);
                Console.WriteLine();
            }

            paramNeedsRedraw = false;
        }

        // Check for param values updated via BLE notify (ff05)
        if (paramValuesUpdated && liveParamValues != null)
        {
            lock (paramValuesLock)
            {
                foreach (var kvp in liveParamValues)
                    currentValues[kvp.Key] = kvp.Value;
                paramValuesUpdated = false;
            }
            paramNeedsRedraw = true;
            continue;
        }

        if (!Console.KeyAvailable)
        {
            await Task.Delay(50);
            continue;
        }

        var key = Console.ReadKey(true);

        switch (key.Key)
        {
            case ConsoleKey.Escape:
                return;

            case ConsoleKey.UpArrow:
                if (paramCursor > 0)
                {
                    paramCursor--;
                    paramNeedsRedraw = true;
                }
                break;

            case ConsoleKey.DownArrow:
                if (paramCursor < paramsList.Count - 1)
                {
                    paramCursor++;
                    paramNeedsRedraw = true;
                }
                break;

            case ConsoleKey.LeftArrow:
            case ConsoleKey.RightArrow:
            {
                var pm = paramsList[paramCursor];
                if (!currentValues.TryGetValue(pm.Id, out var curVal))
                    break;

                bool isRight = key.Key == ConsoleKey.RightArrow;
                byte[] valueBytes;
                string displayValue;

                switch (pm.Type)
                {
                    case "float":
                    {
                        float fv = curVal.GetSingle();
                        float step = (pm.MaxF - pm.MinF) / 20f;
                        if (step <= 0) step = 0.1f;
                        fv = isRight ? fv + step : fv - step;
                        fv = Math.Clamp(fv, pm.MinF, pm.MaxF);
                        valueBytes = BitConverter.GetBytes(fv);
                        displayValue = fv.ToString("F2");
                        break;
                    }
                    case "bool":
                    {
                        int bv = curVal.GetInt32();
                        bv = bv != 0 ? 0 : 1; // toggle
                        valueBytes = BitConverter.GetBytes(bv);
                        displayValue = bv != 0 ? "true" : "false";
                        break;
                    }
                    case "select":
                    {
                        int sv = curVal.GetInt32();
                        int optCount = pm.Options?.Count ?? 1;
                        sv = isRight ? sv + 1 : sv - 1;
                        if (sv < 0) sv = optCount - 1;
                        if (sv >= optCount) sv = 0;
                        valueBytes = BitConverter.GetBytes(sv);
                        displayValue = pm.Options != null && sv >= 0 && sv < pm.Options.Count
                            ? pm.Options[sv] : sv.ToString();
                        break;
                    }
                    default: // int
                    {
                        int iv = curVal.GetInt32();
                        int step = Math.Max(1, (pm.Max - pm.Min) / 20);
                        iv = isRight ? iv + step : iv - step;
                        iv = Math.Clamp(iv, pm.Min, pm.Max);
                        valueBytes = BitConverter.GetBytes(iv);
                        displayValue = iv.ToString();
                        break;
                    }
                }

                // Send SET_PARAM command
                var cmdPayload = new byte[2 + 4];
                cmdPayload[0] = (byte)activeProgram;
                cmdPayload[1] = (byte)pm.Id;
                Array.Copy(valueBytes, 0, cmdPayload, 2, 4);

                try
                {
                    var result = await SendCommand(CMD_SET_PARAM, cmdPayload);
                    using var resultDoc = JsonDocument.Parse(result);
                    if (resultDoc.RootElement.TryGetProperty("ok", out var okElem) && okElem.GetBoolean())
                    {
                        // Update local cache
                        string jsonVal = pm.Type == "float" ? displayValue
                            : pm.Type == "bool" ? (displayValue == "true" ? "1" : "0")
                            : pm.Type == "select" ? currentValues[pm.Id].GetInt32().ToString()
                            : displayValue;

                        // For select, we need the new index
                        if (pm.Type == "select")
                        {
                            int sv = BitConverter.ToInt32(valueBytes, 0);
                            jsonVal = sv.ToString();
                        }

                        using var newValDoc = JsonDocument.Parse(jsonVal);
                        currentValues[pm.Id] = newValDoc.RootElement.Clone();

                        paramStatus = $"{pm.Name} set to {displayValue}";
                        paramStatusColor = ConsoleColor.Green;
                    }
                    else
                    {
                        var err = resultDoc.RootElement.TryGetProperty("err", out var errElem)
                            ? errElem.GetString() ?? "Unknown error"
                            : "Unknown error";
                        paramStatus = $"Error: {err}";
                        paramStatusColor = ConsoleColor.Red;
                    }
                }
                catch (Exception ex)
                {
                    paramStatus = $"Error: {ex.Message}";
                    paramStatusColor = ConsoleColor.Red;
                }

                paramNeedsRedraw = true;
                break;
            }

            case ConsoleKey.Enter:
            {
                // Manual value input
                var pm = paramsList[paramCursor];
                Console.CursorVisible = true;

                // Show input prompt at bottom
                Console.SetCursorPosition(0, Console.CursorTop + 1);
                WriteColored($"  Enter value for {pm.Name}", ConsoleColor.Yellow);

                switch (pm.Type)
                {
                    case "float":
                        WriteColored($" ({pm.MinF:F1}..{pm.MaxF:F1}): ", ConsoleColor.DarkGray);
                        break;
                    case "bool":
                        WriteColored(" (true/false): ", ConsoleColor.DarkGray);
                        break;
                    case "select":
                        var optStr = pm.Options != null ? string.Join(", ", pm.Options.Select((o, i) => $"{i}={o}")) : "";
                        WriteColored($" ({optStr}): ", ConsoleColor.DarkGray);
                        break;
                    default:
                        WriteColored($" ({pm.Min}..{pm.Max}): ", ConsoleColor.DarkGray);
                        break;
                }

                Console.ForegroundColor = ConsoleColor.White;
                var input = Console.ReadLine()?.Trim();
                Console.CursorVisible = false;

                if (string.IsNullOrEmpty(input))
                {
                    paramNeedsRedraw = true;
                    break;
                }

                byte[] valueBytes;
                string displayValue;

                try
                {
                    switch (pm.Type)
                    {
                        case "float":
                            if (!float.TryParse(input, System.Globalization.NumberStyles.Float,
                                    System.Globalization.CultureInfo.InvariantCulture, out var fVal))
                            {
                                paramStatus = "Invalid float value";
                                paramStatusColor = ConsoleColor.Red;
                                paramNeedsRedraw = true;
                                continue;
                            }
                            valueBytes = BitConverter.GetBytes(fVal);
                            displayValue = fVal.ToString("F2");
                            break;
                        case "bool":
                            bool bVal;
                            if (input == "1" || input.Equals("true", StringComparison.OrdinalIgnoreCase))
                                bVal = true;
                            else if (input == "0" || input.Equals("false", StringComparison.OrdinalIgnoreCase))
                                bVal = false;
                            else
                            {
                                paramStatus = "Invalid bool (use true/false)";
                                paramStatusColor = ConsoleColor.Red;
                                paramNeedsRedraw = true;
                                continue;
                            }
                            valueBytes = BitConverter.GetBytes(bVal ? 1 : 0);
                            displayValue = bVal.ToString().ToLower();
                            break;
                        default: // int, select
                            if (!int.TryParse(input, out var iVal))
                            {
                                paramStatus = "Invalid integer value";
                                paramStatusColor = ConsoleColor.Red;
                                paramNeedsRedraw = true;
                                continue;
                            }
                            valueBytes = BitConverter.GetBytes(iVal);
                            displayValue = iVal.ToString();
                            break;
                    }
                }
                catch
                {
                    paramStatus = "Failed to parse value";
                    paramStatusColor = ConsoleColor.Red;
                    paramNeedsRedraw = true;
                    break;
                }

                // Send SET_PARAM command
                var cmdPayload2 = new byte[2 + 4];
                cmdPayload2[0] = (byte)activeProgram;
                cmdPayload2[1] = (byte)pm.Id;
                Array.Copy(valueBytes, 0, cmdPayload2, 2, 4);

                try
                {
                    var result = await SendCommand(CMD_SET_PARAM, cmdPayload2);
                    using var resultDoc = JsonDocument.Parse(result);
                    if (resultDoc.RootElement.TryGetProperty("ok", out var okElem) && okElem.GetBoolean())
                    {
                        // Update local cache
                        string jsonVal = pm.Type == "float" ? displayValue
                            : pm.Type == "bool" ? (displayValue == "true" ? "1" : "0")
                            : displayValue;
                        using var newValDoc = JsonDocument.Parse(jsonVal);
                        currentValues[pm.Id] = newValDoc.RootElement.Clone();

                        paramStatus = $"{pm.Name} set to {displayValue}";
                        paramStatusColor = ConsoleColor.Green;
                    }
                    else
                    {
                        var err = resultDoc.RootElement.TryGetProperty("err", out var errElem)
                            ? errElem.GetString() ?? "Unknown error"
                            : "Unknown error";
                        paramStatus = $"Error: {err}";
                        paramStatusColor = ConsoleColor.Red;
                    }
                }
                catch (Exception ex)
                {
                    paramStatus = $"Error: {ex.Message}";
                    paramStatusColor = ConsoleColor.Red;
                }

                paramNeedsRedraw = true;
                break;
            }
        }
    }
}

// ─── Upload TUI ─────────────────────────────────────────────────────────────

void DrawUploadProgress(int row, int percent, int chunk, int totalChunks, int totalBytes)
{
    Console.SetCursorPosition(0, row);
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.Write("  ");

    int barWidth = BOX_WIDTH - 14;
    int filled = (int)((float)percent / 100 * barWidth);

    WriteColored("[", ConsoleColor.DarkGray);
    for (int i = 0; i < barWidth; i++)
    {
        if (i < filled)
            WriteColored("\u2588", ConsoleColor.Cyan);
        else
            WriteColored("\u2591", ConsoleColor.DarkGray);
    }
    WriteColored("]", ConsoleColor.DarkGray);
    WriteColored($" {percent,3}%", ConsoleColor.White);

    int pad = BOX_WIDTH - 2 - 2 - barWidth - 2 - 5;
    if (pad > 0) Console.Write(new string(' ', pad));
    WriteColored("\u2551", ConsoleColor.DarkCyan);

    // Chunk info line below
    Console.SetCursorPosition(0, row + 1);
    var chunkStr = $"{chunk}/{totalChunks} chunks ({totalBytes} bytes)";
    WriteColored("\u2551", ConsoleColor.DarkCyan);
    Console.Write("  ");
    WriteColored(chunkStr, ConsoleColor.DarkGray);
    int pad2 = BOX_WIDTH - 2 - 2 - chunkStr.Length;
    if (pad2 > 0) Console.Write(new string(' ', pad2));
    WriteColored("\u2551", ConsoleColor.DarkCyan);
}

async Task ShowUploadTUI()
{
    Console.Clear();
    Console.CursorVisible = false;
    Console.ForegroundColor = ConsoleColor.Gray;

    DrawBorderTop();
    DrawLineCustom(() =>
    {
        WriteColored("Upload WASM Program", ConsoleColor.White);
    }, 19);
    DrawBorderMid();
    DrawEmptyLine();
    DrawLineCustom(() =>
    {
        WriteColored("Enter path to .wasm file:", ConsoleColor.Gray);
    }, 24);
    DrawEmptyLine();
    DrawBorderBot();

    Console.CursorVisible = true;
    Console.Write("  > ");
    Console.ForegroundColor = ConsoleColor.White;
    var path = Console.ReadLine()?.Trim();
    Console.CursorVisible = false;

    if (string.IsNullOrEmpty(path))
    {
        SetStatus("Upload cancelled", ConsoleColor.Yellow);
        return;
    }

    // Remove surrounding quotes if present
    if (path.StartsWith('"') && path.EndsWith('"'))
        path = path[1..^1];

    if (!File.Exists(path))
    {
        SetStatus($"File not found: {path}", ConsoleColor.Red);
        return;
    }

    // Draw upload progress screen
    Console.Clear();
    Console.ForegroundColor = ConsoleColor.Gray;

    var fileName = Path.GetFileName(path);

    DrawBorderTop();
    DrawLineCustom(() =>
    {
        WriteColored("Uploading: ", ConsoleColor.Gray);
        WriteColored(fileName.Length > 30 ? fileName[..30] : fileName, ConsoleColor.White);
    }, 11 + Math.Min(fileName.Length, 30));
    DrawBorderMid();
    DrawEmptyLine();

    int progressRow = Console.CursorTop; // Row for the progress bar

    // Write placeholder lines for progress
    DrawEmptyLine(); // progress bar line
    DrawEmptyLine(); // chunk info line
    DrawEmptyLine();
    DrawBorderBot();

    try
    {
        var newId = await UploadWasm(path, progressRow);
        SetStatus($"Upload complete! New program ID: {newId}", ConsoleColor.Green);

        // Refresh program list
        await RefreshPrograms();
    }
    catch (Exception ex)
    {
        SetStatus($"Upload error: {ex.Message}", ConsoleColor.Red);
    }
}

// ─── Delete TUI ─────────────────────────────────────────────────────────────

async Task ShowDeleteTUI()
{
    if (programs.Count == 0)
    {
        SetStatus("No programs to delete", ConsoleColor.Yellow);
        return;
    }

    if (menuCursor < 0 || menuCursor >= programs.Count)
    {
        SetStatus("No program selected", ConsoleColor.Yellow);
        return;
    }

    var target = programs[menuCursor];

    // Show confirmation at bottom of screen
    Console.SetCursorPosition(0, Console.CursorTop);
    WriteColored($"  Delete '{target.Name}' (ID: {target.Id})? ", ConsoleColor.Yellow);
    WriteColored("[Y/N] ", ConsoleColor.White);

    while (true)
    {
        if (!Console.KeyAvailable)
        {
            await Task.Delay(50);
            continue;
        }

        var key = Console.ReadKey(true);
        if (key.Key == ConsoleKey.Y)
        {
            try
            {
                var result = await SendCommand(CMD_DELETE_PROGRAM, new byte[] { (byte)target.Id });
                using var doc = JsonDocument.Parse(result);
                if (doc.RootElement.TryGetProperty("ok", out var okElem) && okElem.GetBoolean())
                {
                    SetStatus($"Deleted '{target.Name}'", ConsoleColor.Green);
                    await RefreshPrograms();
                    if (menuCursor >= programs.Count && menuCursor > 0)
                        menuCursor = programs.Count - 1;
                }
                else
                {
                    var err = doc.RootElement.TryGetProperty("err", out var errElem)
                        ? errElem.GetString() ?? "Unknown error"
                        : "Unknown error";
                    SetStatus($"Error: {err}", ConsoleColor.Red);
                }
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
            }
            break;
        }
        else if (key.Key == ConsoleKey.N || key.Key == ConsoleKey.Escape)
        {
            SetStatus("Delete cancelled", ConsoleColor.Yellow);
            break;
        }
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

string FormatBluetoothAddress(ulong address)
{
    var bytes = BitConverter.GetBytes(address);
    return $"{bytes[5]:X2}:{bytes[4]:X2}:{bytes[3]:X2}:{bytes[2]:X2}:{bytes[1]:X2}:{bytes[0]:X2}";
}

// ─── Device selection ───────────────────────────────────────────────────────

var selectedDevice = await ShowDeviceSelectionScreen();
if (selectedDevice == null)
{
    Console.CursorVisible = true;
    Console.ResetColor();
    Console.Clear();
    Console.WriteLine("No device selected.");
    return;
}

// Show connecting screen
DrawScanningScreen();

// Connect to selected device
try
{
    await ConnectToDevice(selectedDevice.Value.Address, selectedDevice.Value.Name);
    await connectionReady.Task.WaitAsync(cts.Token);
}
catch (OperationCanceledException)
{
    watcher.Stop();
    Console.CursorVisible = true;
    Console.ResetColor();
    return;
}
catch (Exception)
{
    Console.CursorVisible = true;
    Console.ResetColor();
    Console.Clear();
    Console.WriteLine("Failed to connect.");
    return;
}

// ─── Fetch initial data ─────────────────────────────────────────────────────

try
{
    await RefreshPrograms();
}
catch (Exception ex)
{
    SetStatus($"Failed to fetch programs: {ex.Message}", ConsoleColor.Yellow);
}

// ─── Main menu loop (ReadKey-based) ─────────────────────────────────────────

needsRedraw = true;

while (!cts.IsCancellationRequested)
{
    // Check connection
    if (commandChar == null)
    {
        DrawScanningScreen();
        connectionReady = new TaskCompletionSource<bool>();
        try
        {
            await connectionReady.Task.WaitAsync(cts.Token);
            await RefreshPrograms();
            needsRedraw = true;
        }
        catch (OperationCanceledException)
        {
            break;
        }
    }

    if (needsRedraw)
    {
        DrawMainMenuScreen();
        needsRedraw = false;
    }

    if (!Console.KeyAvailable)
    {
        await Task.Delay(50);
        continue;
    }

    var keyInfo = Console.ReadKey(true);

    switch (keyInfo.Key)
    {
        case ConsoleKey.Q:
            goto exitLoop;

        case ConsoleKey.UpArrow:
            if (programs.Count > 0 && menuCursor > 0)
            {
                menuCursor--;
                needsRedraw = true;
            }
            break;

        case ConsoleKey.DownArrow:
            if (programs.Count > 0 && menuCursor < programs.Count - 1)
            {
                menuCursor++;
                needsRedraw = true;
            }
            break;

        case ConsoleKey.Enter:
            if (programs.Count > 0 && menuCursor >= 0 && menuCursor < programs.Count)
            {
                var target = programs[menuCursor];
                try
                {
                    await SetActiveProgram((byte)target.Id);
                    SetStatus($"Switched to: {target.Name}", ConsoleColor.Green);
                }
                catch (Exception ex)
                {
                    SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
                }
                needsRedraw = true;
            }
            break;

        case ConsoleKey.P:
            try
            {
                await ShowParametersMenuTUI();
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
            }
            needsRedraw = true;
            break;

        case ConsoleKey.U:
            try
            {
                await ShowUploadTUI();
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
            }
            needsRedraw = true;
            break;

        case ConsoleKey.D:
            try
            {
                await ShowDeleteTUI();
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}", ConsoleColor.Red);
            }
            needsRedraw = true;
            break;

        case ConsoleKey.R:
            // Hidden: refresh programs
            try
            {
                await RefreshPrograms();
                SetStatus("Programs refreshed", ConsoleColor.Green);
            }
            catch (Exception ex)
            {
                SetStatus($"Refresh error: {ex.Message}", ConsoleColor.Red);
            }
            needsRedraw = true;
            break;

        case ConsoleKey.N:
            // Rename device
            try
            {
                Console.CursorVisible = true;
                Console.SetCursorPosition(0, Console.CursorTop);
                WriteColored($"  New device name (max 20 chars): ", ConsoleColor.Yellow);
                Console.ForegroundColor = ConsoleColor.White;
                var newName = Console.ReadLine()?.Trim();
                Console.CursorVisible = false;

                if (!string.IsNullOrEmpty(newName))
                {
                    if (newName.Length > 20) newName = newName[..20];
                    var nameBytes = Encoding.UTF8.GetBytes(newName);
                    var resp = await SendCommand(CMD_SET_NAME, nameBytes);
                    using var doc = JsonDocument.Parse(resp);
                    if (doc.RootElement.TryGetProperty("ok", out var okElem) && okElem.GetBoolean())
                    {
                        deviceName = newName;
                        SetStatus($"Device renamed to '{newName}'", ConsoleColor.Green);
                    }
                    else
                    {
                        var err = doc.RootElement.TryGetProperty("err", out var errElem)
                            ? errElem.GetString() ?? "Unknown error"
                            : "Unknown error";
                        SetStatus($"Rename error: {err}", ConsoleColor.Red);
                    }
                }
                else
                {
                    SetStatus("Rename cancelled", ConsoleColor.Yellow);
                }
            }
            catch (Exception ex)
            {
                SetStatus($"Rename error: {ex.Message}", ConsoleColor.Red);
            }
            needsRedraw = true;
            break;
    }
}

exitLoop:

// ─── Shutdown ───────────────────────────────────────────────────────────────

Console.Clear();
Console.CursorVisible = true;
Console.ResetColor();
Console.WriteLine("Shutting down...");

watcher.Stop();

// Unsubscribe from notifications
if (responseChar != null)
{
    try
    {
        await responseChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.None);
    }
    catch { }
}

if (activeProgramChar != null)
{
    try
    {
        await activeProgramChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.None);
    }
    catch { }
}

if (paramValuesChar != null)
{
    try
    {
        await paramValuesChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.None);
    }
    catch { }
}

commandChar = null;
responseChar = null;
activeProgramChar = null;
uploadChar = null;
paramValuesChar = null;

// Close GATT service (required for Windows to release connection)
if (connectedService != null)
{
    connectedService.Dispose();
    connectedService = null;
}

// Close device
if (connectedDevice != null)
{
    connectedDevice.Dispose();
    connectedDevice = null;
}

// Give BLE stack time to process disconnect
await Task.Delay(1000);
Console.WriteLine("Disconnected.");

// ─── Data types ─────────────────────────────────────────────────────────────

record ProgramInfo(int Id, string Name);

class ParamMeta
{
    public int Id { get; set; }
    public string Name { get; set; } = "";
    public string Type { get; set; } = "int";
    public int Min { get; set; }
    public int Max { get; set; }
    public float MinF { get; set; }
    public float MaxF { get; set; }
    public List<string>? Options { get; set; }
}

class BleCommandException : Exception
{
    public BleCommandException(string message) : base(message) { }
}
