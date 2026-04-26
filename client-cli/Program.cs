using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using System.Text.Json;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

// ─── BLE UUIDs ───────────────────────────────────────────────────────────────

const string ServiceUuid = "0000ff00-0000-1000-8000-00805f9b34fb";
const string CharCommandUuid = "0000ff01-0000-1000-8000-00805f9b34fb";
const string CharResponseUuid = "0000ff02-0000-1000-8000-00805f9b34fb";
const string CharActiveProgramUuid = "0000ff03-0000-1000-8000-00805f9b34fb";
const string CharUploadUuid = "0000ff04-0000-1000-8000-00805f9b34fb";

var serviceGuid = new Guid(ServiceUuid);
var commandGuid = new Guid(CharCommandUuid);
var responseGuid = new Guid(CharResponseUuid);
var activeProgramGuid = new Guid(CharActiveProgramUuid);
var uploadGuid = new Guid(CharUploadUuid);

// ─── Command codes ──────────────────────────────────────────────────────────

const byte CMD_GET_PROGRAMS = 0x01;
const byte CMD_GET_PARAMS = 0x02;
const byte CMD_SET_PARAM = 0x03;
const byte CMD_GET_PARAM_VALUES = 0x04;
const byte CMD_UPLOAD_START = 0x10;
const byte CMD_UPLOAD_FINISH = 0x11;
const byte CMD_DELETE_PROGRAM = 0x12;
const byte CMD_SET_NAME = 0x20;
const byte CMD_GET_NAME = 0x21;
const byte CMD_GET_HW_CONFIG = 0x22;
const byte CMD_SET_HW_CONFIG = 0x23;
const byte CMD_REBOOT = 0x24;
const byte CMD_GET_META = 0x25;
const byte CMD_SET_META = 0x26;

// ─── Chunked response flags ─────────────────────────────────────────────────

const byte FLAG_FINAL = 0x01;
const byte FLAG_ERROR = 0x02;

const int UPLOAD_CHUNK_SIZE = 200;
const int RESPONSE_TIMEOUT_MS = 5000;
const int CONNECT_TIMEOUT_MS = 10000;

// ─── Parse CLI args ─────────────────────────────────────────────────────────

if (args.Length == 0)
{
    PrintUsage();
    return 1;
}

// Parse --device flag
string? deviceFilter = null;
var filteredArgs = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--device" && i + 1 < args.Length)
    {
        deviceFilter = args[++i];
    }
    else
    {
        filteredArgs.Add(args[i]);
    }
}

if (filteredArgs.Count == 0)
{
    PrintUsage();
    return 1;
}

var command = filteredArgs[0].ToLower();

// ─── State ──────────────────────────────────────────────────────────────────

GattCharacteristic? commandChar = null;
GattCharacteristic? responseChar = null;
GattCharacteristic? activeProgramChar = null;
GattCharacteristic? uploadChar = null;

var responseChunks = new SortedDictionary<byte, byte[]>();
TaskCompletionSource<string>? pendingResponse = null;
object responseLock = new();

// ─── Scan helper ────────────────────────────────────────────────────────────

async Task<List<(ulong Address, string Name)>> ScanDevices(int durationMs = 3000)
{
    var results = new List<(ulong Address, string Name)>();
    object scanLock = new();

    var scanWatcher = new BluetoothLEAdvertisementWatcher
    {
        ScanningMode = BluetoothLEScanningMode.Active
    };
    scanWatcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(serviceGuid);

    scanWatcher.Received += (sender, advArgs) =>
    {
        lock (scanLock)
        {
            if (!results.Any(d => d.Address == advArgs.BluetoothAddress))
            {
                var name = string.IsNullOrEmpty(advArgs.Advertisement.LocalName)
                    ? "Shades LED Lamp"
                    : advArgs.Advertisement.LocalName;
                results.Add((advArgs.BluetoothAddress, name));
            }
        }
    };

    scanWatcher.Start();
    await Task.Delay(durationMs);
    scanWatcher.Stop();

    return results;
}

string FormatAddr(ulong address)
{
    var bytes = BitConverter.GetBytes(address);
    return $"{bytes[5]:X2}:{bytes[4]:X2}:{bytes[3]:X2}:{bytes[2]:X2}:{bytes[1]:X2}:{bytes[0]:X2}";
}

// ─── Handle 'scan' command (no connection needed) ───────────────────────────

if (command == "scan")
{
    Console.Error.Write("Scanning for devices (3s)... ");
    var devices = await ScanDevices();
    Console.Error.WriteLine("done.");

    if (devices.Count == 0)
    {
        Console.WriteLine("No devices found.");
        return 0;
    }

    Console.WriteLine($"{"Name",-24} {"Address"}");
    Console.WriteLine(new string('-', 42));
    foreach (var dev in devices)
    {
        Console.WriteLine($"{dev.Name,-24} {FormatAddr(dev.Address)}");
    }

    return 0;
}

// ─── Connect ────────────────────────────────────────────────────────────────

Console.Error.Write("Scanning for Shades Lamp device... ");

BluetoothLEDevice connectedDevice;

if (deviceFilter != null)
{
    // Scan and find device matching filter
    var devices = await ScanDevices();
    var match = devices.FirstOrDefault(d =>
        d.Name.Contains(deviceFilter, StringComparison.OrdinalIgnoreCase));

    if (match.Address == 0)
    {
        Console.Error.WriteLine("NOT FOUND");
        Console.Error.WriteLine($"No device matching '{deviceFilter}' found.");
        Console.Error.WriteLine("Available devices:");
        foreach (var d in devices)
            Console.Error.WriteLine($"  {d.Name} ({FormatAddr(d.Address)})");
        return 1;
    }

    Console.Error.WriteLine($"found '{match.Name}'!");
    var dev = await BluetoothLEDevice.FromBluetoothAddressAsync(match.Address);
    if (dev == null)
    {
        Console.Error.WriteLine("ERROR: Failed to connect");
        return 1;
    }
    connectedDevice = dev;
}
else
{
    // Connect to first found device (original behavior)
    var connectCts = new CancellationTokenSource(CONNECT_TIMEOUT_MS);
    var connectionReady = new TaskCompletionSource<BluetoothLEDevice>();
    var discoveredDevices = new HashSet<ulong>();

    var scanWatcher = new BluetoothLEAdvertisementWatcher
    {
        ScanningMode = BluetoothLEScanningMode.Active
    };
    scanWatcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(serviceGuid);

    scanWatcher.Received += async (sender, advArgs) =>
    {
        if (!discoveredDevices.Add(advArgs.BluetoothAddress)) return;
        scanWatcher.Stop();

        try
        {
            var device = await BluetoothLEDevice.FromBluetoothAddressAsync(advArgs.BluetoothAddress);
            if (device != null)
                connectionReady.TrySetResult(device);
            else
                connectionReady.TrySetException(new Exception("Failed to connect to device"));
        }
        catch (Exception ex)
        {
            connectionReady.TrySetException(ex);
        }
    };

    scanWatcher.Start();

    try
    {
        connectedDevice = await connectionReady.Task.WaitAsync(connectCts.Token);
    }
    catch (OperationCanceledException)
    {
        scanWatcher.Stop();
        Console.Error.WriteLine("TIMEOUT");
        Console.Error.WriteLine("No Shades Lamp device found within timeout.");
        return 1;
    }
    catch (Exception ex)
    {
        scanWatcher.Stop();
        Console.Error.WriteLine($"ERROR: {ex.Message}");
        return 1;
    }

    Console.Error.WriteLine("found!");
}

// ─── Service discovery ──────────────────────────────────────────────────────

GattDeviceService? service = null;
for (int attempt = 1; attempt <= 5; attempt++)
{
    await Task.Delay(2000);
    try
    {
        var allServices = await connectedDevice.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        if (allServices.Status != GattCommunicationStatus.Success) continue;

        foreach (var s in allServices.Services)
        {
            if (s.Uuid == serviceGuid)
                service = s;
            else
                s.Dispose();
        }
        if (service != null) break;
    }
    catch { }
}

if (service == null)
{
    Console.Error.WriteLine("ERROR: Failed to discover Shades Lamp service.");
    connectedDevice.Dispose();
    return 1;
}

var charsResult = await service.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
if (charsResult.Status != GattCommunicationStatus.Success)
{
    Console.Error.WriteLine("ERROR: Failed to discover characteristics.");
    service.Dispose();
    connectedDevice.Dispose();
    return 1;
}

foreach (var c in charsResult.Characteristics)
{
    if (c.Uuid == commandGuid) commandChar = c;
    else if (c.Uuid == responseGuid) responseChar = c;
    else if (c.Uuid == activeProgramGuid) activeProgramChar = c;
    else if (c.Uuid == uploadGuid) uploadChar = c;
}

if (commandChar == null || responseChar == null || activeProgramChar == null || uploadChar == null)
{
    Console.Error.WriteLine("ERROR: Missing required BLE characteristics.");
    service.Dispose();
    connectedDevice.Dispose();
    return 1;
}

// Subscribe to response notifications
responseChar.ValueChanged += (sender, evtArgs) =>
{
    var data = evtArgs.CharacteristicValue.ToArray();
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
            using var ms = new MemoryStream();
            foreach (var kvp in responseChunks)
                ms.Write(kvp.Value, 0, kvp.Value.Length);
            responseChunks.Clear();

            var json = Encoding.UTF8.GetString(ms.ToArray());
            if (isError)
                pendingResponse?.TrySetException(new Exception(json));
            else
                pendingResponse?.TrySetResult(json);
        }
    }
};

var notifyStatus = await responseChar.WriteClientCharacteristicConfigurationDescriptorAsync(
    GattClientCharacteristicConfigurationDescriptorValue.Notify);

if (notifyStatus != GattCommunicationStatus.Success)
{
    Console.Error.WriteLine("ERROR: Failed to subscribe to response notifications.");
    service.Dispose();
    connectedDevice.Dispose();
    return 1;
}

Console.Error.WriteLine("Connected to Shades Lamp.");

// ─── Execute command ────────────────────────────────────────────────────────

int exitCode = 0;

try
{
    switch (command)
    {
        case "list":
            exitCode = await CmdList();
            break;

        case "upload":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: upload <file.wasm>"); exitCode = 1; break; }
            exitCode = await CmdUpload(filteredArgs[1]);
            break;

        case "delete":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: delete <program-id>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var delId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            exitCode = await CmdDelete(delId);
            break;

        case "activate":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: activate <program-id>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var actId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            exitCode = await CmdActivate(actId);
            break;

        case "params":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: params <program-id>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var parId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            exitCode = await CmdParams(parId);
            break;

        case "set-param":
            if (filteredArgs.Count < 4) { Console.Error.WriteLine("Usage: set-param <program-id> <param-id> <value>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var spProgId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[2], out var spParamId)) { Console.Error.WriteLine("Invalid param ID"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[3], out var spValue)) { Console.Error.WriteLine("Invalid value"); exitCode = 1; break; }
            exitCode = await CmdSetParam(spProgId, spParamId, spValue);
            break;

        case "rename":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: rename <new-name>"); exitCode = 1; break; }
            exitCode = await CmdRename(filteredArgs[1]);
            break;

        case "hw-config":
            exitCode = await CmdHwConfig();
            break;

        case "set-hw-config":
            exitCode = await CmdSetHwConfig(filteredArgs);
            break;

        case "reboot":
            exitCode = await CmdReboot();
            break;

        case "get-meta":
            if (filteredArgs.Count < 2) { Console.Error.WriteLine("Usage: get-meta <program-id>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var gmId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            exitCode = await CmdGetMeta(gmId);
            break;

        case "set-meta":
            if (filteredArgs.Count < 3) { Console.Error.WriteLine("Usage: set-meta <program-id> <meta.json-path>"); exitCode = 1; break; }
            if (!int.TryParse(filteredArgs[1], out var smId)) { Console.Error.WriteLine("Invalid program ID"); exitCode = 1; break; }
            exitCode = await CmdSetMeta(smId, filteredArgs[2]);
            break;

        case "push-meta":
            exitCode = await CmdPushMeta(filteredArgs);
            break;

        default:
            Console.Error.WriteLine($"Unknown command: {command}");
            PrintUsage();
            exitCode = 1;
            break;
    }
}
catch (Exception ex)
{
    Console.Error.WriteLine($"ERROR: {ex.Message}");
    exitCode = 1;
}

// ─── Cleanup ────────────────────────────────────────────────────────────────

try
{
    await responseChar.WriteClientCharacteristicConfigurationDescriptorAsync(
        GattClientCharacteristicConfigurationDescriptorValue.None);
}
catch { }

service.Dispose();
connectedDevice.Dispose();
await Task.Delay(500);

return exitCode;

// ─── Command implementations ────────────────────────────────────────────────

async Task<int> CmdList()
{
    var json = await SendCommand(CMD_GET_PROGRAMS);

    // Read active program
    int activeId = -1;
    try
    {
        var readResult = await activeProgramChar!.ReadValueAsync(BluetoothCacheMode.Uncached);
        if (readResult.Status == GattCommunicationStatus.Success)
        {
            var data = readResult.Value.ToArray();
            if (data.Length >= 1) activeId = data[0];
        }
    }
    catch { }

    using var doc = JsonDocument.Parse(json);
    Console.WriteLine($"{"ID",-4} {"Name",-30} {"Status"}");
    Console.WriteLine(new string('-', 44));

    foreach (var elem in doc.RootElement.EnumerateArray())
    {
        var id = elem.GetProperty("id").GetInt32();
        var name = elem.GetProperty("name").GetString() ?? $"Program {id}";
        var status = id == activeId ? "ACTIVE" : "";
        Console.WriteLine($"{id,-4} {name,-30} {status}");
    }

    return 0;
}

async Task<int> CmdUpload(string filePath)
{
    if (!File.Exists(filePath))
    {
        Console.Error.WriteLine($"File not found: {filePath}");
        return 1;
    }

    var fileData = await File.ReadAllBytesAsync(filePath);
    Console.Error.WriteLine($"Uploading {Path.GetFileName(filePath)} ({fileData.Length} bytes)...");

    // UPLOAD_START
    var sizeBytes = BitConverter.GetBytes((uint)fileData.Length);
    var startResp = await SendCommand(CMD_UPLOAD_START, sizeBytes);

    using (var doc = JsonDocument.Parse(startResp))
    {
        if (!doc.RootElement.TryGetProperty("ok", out var ok) || !ok.GetBoolean())
        {
            var err = doc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
            Console.Error.WriteLine($"Upload start failed: {err}");
            return 1;
        }
    }

    // Send chunks
    int offset = 0;
    int chunkIndex = 0;
    int totalChunks = (fileData.Length + UPLOAD_CHUNK_SIZE - 1) / UPLOAD_CHUNK_SIZE;

    while (offset < fileData.Length)
    {
        int chunkLen = Math.Min(UPLOAD_CHUNK_SIZE, fileData.Length - offset);
        var writer = new DataWriter();
        writer.WriteBytes(fileData.AsSpan(offset, chunkLen).ToArray());

        var writeResult = await uploadChar!.WriteValueAsync(
            writer.DetachBuffer(), GattWriteOption.WriteWithoutResponse);

        if (writeResult != GattCommunicationStatus.Success)
        {
            Console.Error.WriteLine($"Chunk write failed at offset {offset}");
            return 1;
        }

        offset += chunkLen;
        chunkIndex++;

        int percent = (int)((long)offset * 100 / fileData.Length);
        Console.Error.Write($"\r  [{chunkIndex}/{totalChunks}] {percent}%");

        if (chunkIndex % 10 == 0)
            await Task.Delay(20);
    }

    Console.Error.WriteLine();

    // UPLOAD_FINISH
    var finishResp = await SendCommand(CMD_UPLOAD_FINISH);

    using var finishDoc = JsonDocument.Parse(finishResp);
    if (finishDoc.RootElement.TryGetProperty("ok", out var finishOk) && finishOk.GetBoolean())
    {
        var newId = finishDoc.RootElement.TryGetProperty("id", out var idElem) ? idElem.GetInt32() : -1;
        Console.WriteLine($"OK id={newId}");
        return 0;
    }
    else
    {
        var err = finishDoc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
        Console.Error.WriteLine($"Upload finish failed: {err}");
        return 1;
    }
}

async Task<int> CmdDelete(int programId)
{
    var resp = await SendCommand(CMD_DELETE_PROGRAM, new byte[] { (byte)programId });
    using var doc = JsonDocument.Parse(resp);

    if (doc.RootElement.TryGetProperty("ok", out var ok) && ok.GetBoolean())
    {
        Console.WriteLine($"Deleted program {programId}");
        return 0;
    }

    var err = doc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
    Console.Error.WriteLine($"Delete failed: {err}");
    return 1;
}

async Task<int> CmdActivate(int programId)
{
    var writer = new DataWriter();
    writer.WriteByte((byte)programId);

    var result = await activeProgramChar!.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
    if (result != GattCommunicationStatus.Success)
    {
        Console.Error.WriteLine($"Failed to set active program: {result}");
        return 1;
    }

    Console.WriteLine($"Activated program {programId}");
    return 0;
}

async Task<int> CmdParams(int programId)
{
    var paramsJson = await SendCommand(CMD_GET_PARAMS, new byte[] { (byte)programId });
    var valuesJson = await SendCommand(CMD_GET_PARAM_VALUES, new byte[] { (byte)programId });

    // Parse values
    var values = new Dictionary<int, JsonElement>();
    using (var valDoc = JsonDocument.Parse(valuesJson))
    {
        foreach (var prop in valDoc.RootElement.EnumerateObject())
        {
            if (int.TryParse(prop.Name, out var pid))
                values[pid] = prop.Value.Clone();
        }
    }

    using var parDoc = JsonDocument.Parse(paramsJson);
    Console.WriteLine($"{"ID",-4} {"Name",-16} {"Type",-8} {"Value"}");
    Console.WriteLine(new string('-', 44));

    foreach (var elem in parDoc.RootElement.EnumerateArray())
    {
        var id = elem.GetProperty("id").GetInt32();
        var name = elem.GetProperty("name").GetString() ?? "?";
        var type = elem.GetProperty("type").GetString() ?? "int";

        string valueStr = "?";
        if (values.TryGetValue(id, out var val))
        {
            if (type == "float")
                valueStr = val.GetSingle().ToString("F2");
            else if (type == "select")
            {
                var sv = val.GetInt32();
                if (elem.TryGetProperty("options", out var opts))
                {
                    var optArr = opts.EnumerateArray().Select(o => o.GetString()).ToList();
                    valueStr = sv >= 0 && sv < optArr.Count ? $"{sv} ({optArr[sv]})" : sv.ToString();
                }
                else valueStr = sv.ToString();
            }
            else
                valueStr = val.GetInt32().ToString();
        }

        Console.WriteLine($"{id,-4} {name,-16} {type,-8} {valueStr}");
    }

    return 0;
}

async Task<int> CmdSetParam(int programId, int paramId, int value)
{
    var payload = new byte[6];
    payload[0] = (byte)programId;
    payload[1] = (byte)paramId;
    BitConverter.GetBytes(value).CopyTo(payload, 2);

    var resp = await SendCommand(CMD_SET_PARAM, payload);
    using var doc = JsonDocument.Parse(resp);

    if (doc.RootElement.TryGetProperty("ok", out var ok) && ok.GetBoolean())
    {
        Console.WriteLine($"OK param[{paramId}]={value}");
        return 0;
    }

    var err = doc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
    Console.Error.WriteLine($"Set param failed: {err}");
    return 1;
}

async Task<int> CmdRename(string newName)
{
    if (newName.Length > 20) newName = newName[..20];
    var nameBytes = Encoding.UTF8.GetBytes(newName);
    var resp = await SendCommand(CMD_SET_NAME, nameBytes);

    using var doc = JsonDocument.Parse(resp);
    if (doc.RootElement.TryGetProperty("ok", out var ok) && ok.GetBoolean())
    {
        Console.WriteLine($"Device renamed to '{newName}'. Reboot device to apply.");
        return 0;
    }

    var err = doc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
    Console.Error.WriteLine($"Rename failed: {err}");
    return 1;
}

async Task<int> CmdHwConfig()
{
    var resp = await SendCommand(CMD_GET_HW_CONFIG);
    using var doc = JsonDocument.Parse(resp);
    var root = doc.RootElement;

    if (!root.TryGetProperty("ok", out var ok) || !ok.GetBoolean())
    {
        var err = root.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
        Console.Error.WriteLine($"Failed: {err}");
        return 1;
    }

    var pin = root.GetProperty("pin").GetInt32();
    var width = root.GetProperty("width").GetInt32();
    var height = root.GetProperty("height").GetInt32();
    var zigzag = root.TryGetProperty("zigzag", out var zz) && zz.GetBoolean();

    Console.WriteLine($"Pin: {pin}, Size: {width}x{height}, Zigzag: {(zigzag ? "on" : "off")}");
    return 0;
}

async Task<int> CmdSetHwConfig(List<string> cliArgs)
{
    // Get current config first
    var currentResp = await SendCommand(CMD_GET_HW_CONFIG);
    using var currentDoc = JsonDocument.Parse(currentResp);
    var cur = currentDoc.RootElement;

    if (!cur.TryGetProperty("ok", out var curOk) || !curOk.GetBoolean())
    {
        Console.Error.WriteLine("Failed to read current config");
        return 1;
    }

    int pin = cur.GetProperty("pin").GetInt32();
    int width = cur.GetProperty("width").GetInt32();
    int height = cur.GetProperty("height").GetInt32();
    bool zigzag = cur.TryGetProperty("zigzag", out var zzVal) && zzVal.GetBoolean();

    // Parse optional args: --pin N --width N --height N --zigzag --no-zigzag
    bool anySet = false;
    for (int i = 1; i < cliArgs.Count; i++)
    {
        if (cliArgs[i] == "--pin" && i + 1 < cliArgs.Count)
        {
            if (!int.TryParse(cliArgs[++i], out pin)) { Console.Error.WriteLine("Invalid pin value"); return 1; }
            anySet = true;
        }
        else if (cliArgs[i] == "--width" && i + 1 < cliArgs.Count)
        {
            if (!int.TryParse(cliArgs[++i], out width)) { Console.Error.WriteLine("Invalid width value"); return 1; }
            anySet = true;
        }
        else if (cliArgs[i] == "--height" && i + 1 < cliArgs.Count)
        {
            if (!int.TryParse(cliArgs[++i], out height)) { Console.Error.WriteLine("Invalid height value"); return 1; }
            anySet = true;
        }
        else if (cliArgs[i] == "--zigzag")
        {
            zigzag = true;
            anySet = true;
        }
        else if (cliArgs[i] == "--no-zigzag")
        {
            zigzag = false;
            anySet = true;
        }
    }

    if (!anySet)
    {
        Console.Error.WriteLine("Usage: set-hw-config --pin <N> --width <N> --height <N> [--zigzag|--no-zigzag]");
        Console.Error.WriteLine("At least one parameter is required.");
        return 1;
    }

    // Build payload: pin(1) + width(2 LE) + height(2 LE) + zigzag(1)
    var payload = new byte[6];
    payload[0] = (byte)pin;
    BitConverter.GetBytes((ushort)width).CopyTo(payload, 1);
    BitConverter.GetBytes((ushort)height).CopyTo(payload, 3);
    payload[5] = (byte)(zigzag ? 1 : 0);

    var resp = await SendCommand(CMD_SET_HW_CONFIG, payload);
    using var doc = JsonDocument.Parse(resp);
    var root = doc.RootElement;

    if (root.TryGetProperty("ok", out var ok) && ok.GetBoolean())
    {
        string zigzagStr = zigzag ? ", zigzag" : "";
        Console.WriteLine($"Hardware config updated (pin={pin}, {width}x{height}{zigzagStr}). Reboot device to apply.");
        return 0;
    }

    var err = root.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
    Console.Error.WriteLine($"Failed: {err}");
    return 1;
}

async Task<int> CmdGetMeta(int programId)
{
    var resp = await SendCommand(CMD_GET_META, new byte[] { (byte)programId });
    Console.WriteLine(resp);
    return 0;
}

async Task<int> CmdSetMeta(int programId, string filePath)
{
    if (!File.Exists(filePath))
    {
        Console.Error.WriteLine($"File not found: {filePath}");
        return 1;
    }

    var json = await File.ReadAllTextAsync(filePath);
    if (json.Length > 2048)
    {
        Console.Error.WriteLine($"Meta too large ({json.Length} bytes, max 2048)");
        return 1;
    }

    var payload = new byte[1 + Encoding.UTF8.GetByteCount(json)];
    payload[0] = (byte)programId;
    Encoding.UTF8.GetBytes(json, 0, json.Length, payload, 1);

    var resp = await SendCommand(CMD_SET_META, payload);
    using var doc = JsonDocument.Parse(resp);

    if (doc.RootElement.TryGetProperty("ok", out var ok) && ok.GetBoolean())
    {
        Console.WriteLine($"Meta set for program {programId}");
        return 0;
    }

    var err = doc.RootElement.TryGetProperty("err", out var e) ? e.GetString() : "Unknown";
    Console.Error.WriteLine($"Set meta failed: {err}");
    return 1;
}

async Task<int> CmdPushMeta(List<string> cliArgs)
{
    // push-meta: uploads meta.json for all programs on device from local programs/ directory
    // Optional: push-meta <directory> (defaults to programs/ relative to exe)
    string programsDir;
    if (cliArgs.Count >= 2 && Directory.Exists(cliArgs[1]))
        programsDir = cliArgs[1];
    else
        programsDir = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "programs");

    if (!Directory.Exists(programsDir))
    {
        Console.Error.WriteLine($"Programs directory not found: {programsDir}");
        Console.Error.WriteLine("Usage: push-meta [programs-directory]");
        return 1;
    }

    // Get program list from device
    var listJson = await SendCommand(CMD_GET_PROGRAMS);
    using var listDoc = JsonDocument.Parse(listJson);

    int success = 0, skipped = 0, failed = 0;

    foreach (var elem in listDoc.RootElement.EnumerateArray())
    {
        var id = elem.GetProperty("id").GetInt32();
        var name = elem.GetProperty("name").GetString() ?? "";

        // Find matching meta.json by scanning directories
        string? metaPath = null;
        foreach (var dir in Directory.GetDirectories(programsDir))
        {
            var candidate = Path.Combine(dir, "meta.json");
            if (!File.Exists(candidate)) continue;

            try
            {
                var metaContent = File.ReadAllText(candidate);
                using var metaDoc = JsonDocument.Parse(metaContent);
                var metaName = metaDoc.RootElement.TryGetProperty("name", out var mn) ? mn.GetString() : null;

                // Match by meta name (exact, case-insensitive)
                if (metaName != null && string.Equals(metaName, name, StringComparison.OrdinalIgnoreCase))
                {
                    metaPath = candidate;
                    break;
                }
                // Match by directory name (underscores → spaces)
                var dirName = Path.GetFileName(dir).Replace("_", " ");
                if (string.Equals(dirName, name, StringComparison.OrdinalIgnoreCase))
                {
                    metaPath = candidate;
                    break;
                }
                // Match by containment (e.g. "Rainbow Comet" contains "comet")
                var slug = Path.GetFileName(dir).ToLower();
                if (name.ToLower().Contains(slug) || slug.Contains(name.ToLower().Replace(" ", "")))
                {
                    metaPath = candidate;
                    break;
                }
            }
            catch { }
        }

        if (metaPath == null)
        {
            Console.Error.WriteLine($"  [{id}] {name}: no meta.json found, skipping");
            skipped++;
            continue;
        }

        var json = File.ReadAllText(metaPath);
        if (json.Length > 2048)
        {
            Console.Error.WriteLine($"  [{id}] {name}: meta too large ({json.Length}), skipping");
            skipped++;
            continue;
        }

        var payload = new byte[1 + Encoding.UTF8.GetByteCount(json)];
        payload[0] = (byte)id;
        Encoding.UTF8.GetBytes(json, 0, json.Length, payload, 1);

        try
        {
            var resp = await SendCommand(CMD_SET_META, payload);
            using var doc = JsonDocument.Parse(resp);
            if (doc.RootElement.TryGetProperty("ok", out var ok) && ok.GetBoolean())
            {
                Console.WriteLine($"  [{id}] {name}: OK");
                success++;
            }
            else
            {
                Console.Error.WriteLine($"  [{id}] {name}: FAILED");
                failed++;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"  [{id}] {name}: ERROR ({ex.Message})");
            failed++;
        }
    }

    Console.WriteLine($"\nDone: {success} uploaded, {skipped} skipped, {failed} failed");
    return failed > 0 ? 1 : 0;
}

async Task<int> CmdReboot()
{
    try
    {
        await SendCommand(CMD_REBOOT);
    }
    catch
    {
        // Device reboots and drops BLE connection — expected
    }
    Console.WriteLine("Device is rebooting...");
    return 0;
}

// ─── BLE helpers ────────────────────────────────────────────────────────────

async Task<string> SendCommand(byte commandCode, byte[]? payload = null)
{
    if (commandChar == null) throw new InvalidOperationException("Not connected");

    TaskCompletionSource<string> tcs;
    lock (responseLock)
    {
        responseChunks.Clear();
        tcs = new TaskCompletionSource<string>();
        pendingResponse = tcs;
    }

    var writer = new DataWriter();
    writer.WriteByte(commandCode);
    if (payload != null) writer.WriteBytes(payload);

    var writeResult = await commandChar.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
    if (writeResult != GattCommunicationStatus.Success)
    {
        lock (responseLock) { pendingResponse = null; responseChunks.Clear(); }
        throw new IOException($"BLE write failed: {writeResult}");
    }

    using var timeoutCts = new CancellationTokenSource(RESPONSE_TIMEOUT_MS);

    try
    {
        var completed = await Task.WhenAny(tcs.Task, Task.Delay(Timeout.Infinite, timeoutCts.Token));
        if (completed == tcs.Task) return await tcs.Task;
        throw new TimeoutException("Response timeout");
    }
    catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested)
    {
        throw new TimeoutException("Response timeout");
    }
    finally
    {
        lock (responseLock)
        {
            if (pendingResponse == tcs) { pendingResponse = null; responseChunks.Clear(); }
        }
    }
}

// ─── Usage ──────────────────────────────────────────────────────────────────

void PrintUsage()
{
    Console.Error.WriteLine("Shades Lamp CLI - Command-line tool for Shades Lamp device management");
    Console.Error.WriteLine();
    Console.Error.WriteLine("Usage: shades-cli [--device <name>] <command> [args]");
    Console.Error.WriteLine();
    Console.Error.WriteLine("Options:");
    Console.Error.WriteLine("  --device <name>                   Connect to device matching name (substring)");
    Console.Error.WriteLine();
    Console.Error.WriteLine("Commands:");
    Console.Error.WriteLine("  scan                              Scan and list nearby devices");
    Console.Error.WriteLine("  list                              List all programs on device");
    Console.Error.WriteLine("  upload <file.wasm>                Upload a WASM program");
    Console.Error.WriteLine("  delete <program-id>               Delete a program");
    Console.Error.WriteLine("  activate <program-id>             Set active program");
    Console.Error.WriteLine("  params <program-id>               Show program parameters and values");
    Console.Error.WriteLine("  set-param <prog-id> <par-id> <val>  Set a parameter value");
    Console.Error.WriteLine("  rename <new-name>                 Rename the device (max 20 chars)");
    Console.Error.WriteLine("  hw-config                         Show hardware config (pin, size)");
    Console.Error.WriteLine("  set-hw-config [options]            Set hardware config (reboot required)");
    Console.Error.WriteLine("    --pin <N>                         LED data pin (0-48)");
    Console.Error.WriteLine("    --width <N>                       Matrix width (1-1024)");
    Console.Error.WriteLine("    --height <N>                      Matrix height (1-1024)");
    Console.Error.WriteLine("    --zigzag                          Serpentine/zigzag wiring");
    Console.Error.WriteLine("    --no-zigzag                       Linear wiring (default)");
    Console.Error.WriteLine("  get-meta <program-id>             Get program meta.json from device");
    Console.Error.WriteLine("  set-meta <program-id> <file>      Upload meta.json to device");
    Console.Error.WriteLine("  push-meta [programs-dir]          Push meta.json for all programs on device");
    Console.Error.WriteLine("  reboot                            Reboot the device");
}
