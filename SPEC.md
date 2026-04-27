# Shades Lamp — WASM-программируемый LED контроллер на ESP32-S3

## Обзор

Прошивка для ESP32-S3 (N16R8), которая выполняет WASM-программы для управления адресными светодиодами WS2812. Программы загружаются по BLE, хранятся в LittleFS. Управление через BLE: выбор программы, настройка параметров.

**Железо:** ESP32-S3-DevKitC-1 N16R8 (16MB Flash, 8MB PSRAM), WS2812 на GPIO 48 (настраивается).
**Матрица:** произвольный размер (настраивается через BLE), поддержка zigzag-проводки.
**Имя устройства:** "Shades Lamp" (по умолчанию "Shades LED Lamp").

---

## Архитектура

```
+---------------------------------------------+
|                 ESP32-S3                     |
|                                              |
|  +----------+  +----------+  +-----------+  |
|  | BLE GATT |--| Program  |--| WASM3     |  |
|  | Service  |  | Manager  |  | Runtime   |  |
|  +----------+  +----------+  +-----+-----+  |
|                                    |         |
|  +----------+  +----------+  +-----v-----+  |
|  | LittleFS |  | Param    |  | LED       |  |
|  | Storage  |  | Store    |  | Driver    |  |
|  +----------+  +----------+  +-----------+  |
+---------------------------------------------+
         ^ BLE
         |
+--------v--------+    +------------------+
|  C# CLI Client  |    |  C# Console App  |
| (client-cli/)   |    | (client-console/)|
+-----------------+    +------------------+
```

### Компоненты

| Компонент | Ответственность |
|-----------|----------------|
| **LED Driver** | Абстракция над NeoPixel. Фреймбуфер `width * height * 3` байт. Методы `setPixel(x, y, r, g, b)` и `show()`. Поддержка zigzag-проводки |
| **WASM3 Runtime** | Загрузка и исполнение .wasm программ. Экспорт host-функций, вызов `update()` на каждом тике |
| **Program Manager** | Загрузка/удаление программ из LittleFS, асинхронное переключение активной программы, управление жизненным циклом |
| **Param Store** | Хранение текущих значений параметров активной программы в RAM. Сохранение в config.json с throttling (не чаще 1 раз в 2 сек) |
| **BLE Service** | GATT-сервер: команды, ответы, загрузка WASM, уведомления. MTU negotiation |
| **LittleFS Storage** | Файловая система во flash для .wasm файлов и конфигурации |

---

## WASM Program API

### Система координат

- **Y=0 — это НИЗ физического дисплея**, Y=H-1 — верх
- "Падающие" эффекты = уменьшение Y, "поднимающиеся" = увеличение Y
- X=0 — начало LED-ленты, X оборачивается по цилиндру

### Host-функции (импортируются WASM-программой из модуля `env`)

```c
// Размер матрицы
int      get_width();           // Ширина матрицы
int      get_height();          // Высота матрицы

// Управление пикселями
void     set_pixel(int x, int y, int r, int g, int b);
void     draw();                // Применить изменения (flush framebuffer -> LED strip)

// Параметры
int32_t  get_param_i32(int param_id);   // Чтение int/bool/select параметра
float    get_param_f32(int param_id);   // Чтение float параметра
void     set_param_i32(int param_id, int value);  // Запись параметра из программы
```

### Экспортируемые функции (WASM -> Host)

```c
void     init();                    // Вызывается один раз при загрузке программы
void     update(int32_t tick_ms);   // Вызывается каждый тик (~33ms при 30 FPS)

// Метаданные программы (указатель и длина JSON-строки в линейной памяти WASM)
int32_t  get_meta_ptr();
int32_t  get_meta_len();
```

### tick_ms — ВАЖНО

`tick_ms` — это **полное время в миллисекундах с момента старта программы**, а НЕ дельта между кадрами.

Два паттерна использования:

**1. Время как фаза (для синусоид, вращений)** — используем tick_ms напрямую:
```c
float t = (float)tick_ms * (float)speed * 0.00003f;
float wave = fsin(t);
```

**2. Физика с dt (гравитация, скорость)** — вычисляем дельту вручную:
```c
static int32_t prev_tick;
// В init(): prev_tick = 0;
// В update():
int32_t delta_ms = tick_ms - prev_tick;
if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
prev_tick = tick_ms;
float dt = (float)delta_ms / 1000.0f;
```

### Формат метаданных (JSON в WASM memory)

```json
{
  "name": "Rainbow",
  "desc": "Smooth HSV rainbow",
  "params": [
    {
      "id": 0,
      "name": "Speed",
      "type": "int",
      "min": 1,
      "max": 100,
      "default": 50,
      "desc": "Animation speed"
    },
    {
      "id": 1,
      "name": "Mode",
      "type": "int",
      "min": 0,
      "max": 2,
      "options": ["Wave", "Solid", "Pulse"],
      "default": 0,
      "desc": "Display mode"
    }
  ]
}
```

**Типы параметров:**

| Тип | Хранение | Поля | WASM-чтение |
|-----|----------|------|-------------|
| `int` | int32 | `min`, `max`, `default` | `get_param_i32(id)` |
| `float` | float32 | `min`, `max`, `default` | `get_param_f32(id)` |
| `select` | int32 (index) | `options[]`, `default`, `min`=0, `max`=N-1 | `get_param_i32(id)` |
| switch (On/Off) | int32 (0/1) | `options: ["Off","On"]`, `default` | `get_param_i32(id)` |

### WASM-компиляция

```bash
clang --target=wasm32 -nostdlib -O2 \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined \
  -I programs/common \
  -o programs/<name>/main.wasm programs/<name>/main.c
```

**Ограничения:**
- Нет stdlib (нужны свои sin, sqrt, rand и т.д.)
- WASM memory: 1 page (64 KB) — статические массивы живут здесь, не на C-стеке
- Максимальный размер .wasm: ~64 KB
- `MAX_W=64`, `MAX_H=64` для статических фреймбуферов

---

## BLE протокол

### Service UUID

```
SERVICE:  0000ff00-0000-1000-8000-00805f9b34fb
```

### Characteristics

| UUID | Имя | Свойства | Описание |
|------|-----|----------|----------|
| `ff01` | Command | WRITE | Клиент отправляет команды |
| `ff02` | Response | NOTIFY | Устройство отправляет ответы (с чанкированием) |
| `ff03` | Active Program | READ / WRITE / NOTIFY | Текущий ID программы (uint8). Запись переключает программу |
| `ff04` | Upload | WRITE_NR | Чанки WASM-файла при загрузке |
| `ff05` | Param Values | NOTIFY | Уведомление об изменении параметров |

### Формат Response (чанкированный)

Каждый NOTIFY-пакет:
```
[seq: uint8][flags: uint8][payload]
```
- `seq` — номер чанка (0, 1, 2, ...)
- `flags`: bit 0 = последний чанк (FINAL), bit 1 = ошибка (ERROR)
- `payload` — данные (до MTU-2 байт)

### Команды

| Код | Команда | Payload | Ответ |
|-----|---------|---------|-------|
| `0x01` | GET_PROGRAMS | — | JSON: `[{"id":0,"name":"RGB Cycle"},...]` |
| `0x02` | GET_PARAMS | `[program_id: uint8]` | JSON: массив параметров из мета |
| `0x03` | SET_PARAM | `[program_id: uint8, param_id: uint8, value: 4 bytes LE]` | `{"ok":true}` |
| `0x04` | GET_PARAM_VALUES | `[program_id: uint8]` | JSON: `{"0":50,"1":128,...}` |
| `0x10` | UPLOAD_START | `[total_size: uint32_le]` | `{"ok":true}` |
| `0x11` | UPLOAD_FINISH | — | `{"ok":true,"id":3}` |
| `0x12` | DELETE_PROGRAM | `[program_id: uint8]` | `{"ok":true}` |
| `0x20` | SET_NAME | `[name: string]` | `{"ok":true}` |
| `0x21` | GET_NAME | — | `{"ok":true,"name":"Shades Lamp"}` |
| `0x22` | GET_HW_CONFIG | — | `{"ok":true,"pin":48,"width":16,"height":16,"zigzag":true}` |
| `0x23` | SET_HW_CONFIG | `[pin:1, width:2 LE, height:2 LE, flags:1]` | `{"ok":true,"reboot":true}` |
| `0x30` | REBOOT | — | `{"ok":true}` |

---

## Хранение (LittleFS)

```
/
+-- programs/
|   +-- 0.wasm          # Программа с ID=0
|   +-- 1.wasm
|   +-- ...
+-- config.json          # Настройки: активная программа, hw config, параметры
```

### config.json

```json
{
  "active": 7,
  "ledPin": 48,
  "ledWidth": 16,
  "ledHeight": 16,
  "zigzag": true,
  "name": "Shades Lamp",
  "params": {
    "7": {"0": 25, "1": 200},
    "8": {"0": 50}
  }
}
```

---

## Структура проекта

```
esp32-led-test/
+-- firmware/
|   +-- firmware.ino              # Главный скетч: setup(), renderTask()
|   +-- led_driver.h / .cpp       # LED Driver: фреймбуфер, NeoPixel, zigzag
|   +-- wasm_engine.h / .cpp      # WASM3 обёртка: загрузка, host-функции, tick
|   +-- program_manager.h / .cpp  # Управление программами: load, switch, delete
|   +-- ble_service.h / .cpp      # BLE GATT сервер
|   +-- storage.h / .cpp          # LittleFS + config
|   +-- param_store.h / .cpp      # Параметры программ в RAM
|   +-- partitions.csv            # Custom partition table (4MB app + 12MB spiffs)
+-- programs/                      # Исходники WASM-программ (C)
|   +-- common/
|   |   +-- api.h                 # Объявления host-функций
|   +-- aurora/
|   +-- bouncing_balls/
|   +-- breathing/
|   +-- clouds/
|   +-- comet/
|   +-- confetti/
|   +-- core/
|   +-- cube/
|   +-- dna/
|   +-- electrons/
|   +-- fireflies/
|   +-- flame/
|   +-- flame_particle/
|   +-- helix/
|   +-- hexagons/
|   +-- jellyfish/
|   +-- kaleidoscope/
|   +-- lava/
|   +-- lava_lamp/
|   +-- matrix_rain/
|   +-- matrix_test/
|   +-- metaballs/
|   +-- moire/
|   +-- morphing/
|   +-- ocean/
|   +-- plasma/
|   +-- police/
|   +-- popcorn/
|   +-- pulse/
|   +-- rainbow/
|   +-- random_blink/
|   +-- rgb_cycle/
|   +-- sinusoid/
|   +-- snow/
|   +-- solid_color/
|   +-- spirals/
|   +-- starfall/
|   +-- colored_rain/
|   +-- tricolor/
|   +-- twinkling/
|   +-- vortex/
|   +-- warp/
|   +-- waves/
+-- client-cli/                   # CLI-клиент (C#)
|   +-- WasmLedCli.csproj
|   +-- Program.cs
+-- client-console/               # Консольный клиент (C#)
|   +-- WasmLedClient.csproj
|   +-- Program.cs
+-- app/                          # MAUI-приложение
+-- SPEC.md
+-- .gitignore
```

---

## FreeRTOS задачи

| Задача | Core | Приоритет | Stack | Описание |
|--------|------|-----------|-------|----------|
| `renderTask` | 1 | 2 | **64 KB** | Основной цикл: `processPending()` + `wasm_update()` каждые ~33ms (30 FPS) |
| BLE (Arduino) | 0 | 1 | default | Обработка BLE-событий |

**Важно:** Arduino `loop()` на Core 1 вытесняется renderTask (приоритет 2). Вся логика (переключение программ, тики, уведомления) выполняется внутри `renderTask`. `loop()` содержит только `vTaskDelay(1000)`.

Stack renderTask = 64 KB, потому что `switchProgram()` (JSON-парсинг + инициализация wasm3) и `tick()` (интерпретатор wasm3) выполняются в одном таске.

---

## C# клиенты

### CLI (client-cli/)

```bash
dotnet run --project client-cli/ -- <command> [args]
```

Команды: `scan`, `list`, `upload <file.wasm>`, `delete <id>`, `activate <id>`, `params <id>`, `set-param <prog> <par> <val>`, `rename <name>`, `hw-config`, `set-hw-config`, `reboot`.

### Console (client-console/)

Интерактивный TUI с меню.

### Зависимости

- .NET 9.0, `net9.0-windows10.0.19041.0`, win-x64
- `Windows.Devices.Bluetooth` (Windows SDK, без NuGet)

---

## Сборка

### Прошивка

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom" firmware/
arduino-cli upload --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom" --port COM13 firmware/
```

### WASM-программы

```bash
"C:\Program Files\LLVM\bin\clang.exe" --target=wasm32 -nostdlib -O2 \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined \
  -I programs/common \
  -o programs/<name>/main.wasm programs/<name>/main.c
```

### C# клиент

```bash
dotnet build client-cli/
dotnet run --project client-cli/ -- list
```
