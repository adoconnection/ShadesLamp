# WasmLED — WASM-программируемый LED контроллер на ESP32-S3

## Обзор

Прошивка для ESP32-S3 (N16R8), которая выполняет WASM-программы для управления адресными светодиодами WS2812. Программы загружаются по BLE, хранятся в LittleFS. Управление через BLE: выбор программы, настройка параметров.

**Железо:** ESP32-S3-DevKitC-1 N16R8 (16MB Flash, 8MB PSRAM), 1× WS2812 на GPIO 48.
**Будущее:** матрица 32×32 (1024 LED). API проектируется с учётом этого.

---

## Архитектура

```
┌─────────────────────────────────────────────┐
│                 ESP32-S3                     │
│                                              │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │ BLE GATT │──│ Program  │──│  WASM3    │  │
│  │ Service  │  │ Manager  │  │  Runtime  │  │
│  └──────────┘  └──────────┘  └─────┬─────┘  │
│                                    │         │
│  ┌──────────┐  ┌──────────┐  ┌─────▼─────┐  │
│  │ LittleFS │  │ Param    │  │ LED       │  │
│  │ Storage  │  │ Store    │  │ Driver    │  │
│  └──────────┘  └──────────┘  └───────────┘  │
└─────────────────────────────────────────────┘
         ▲ BLE
         │
┌────────▼────────┐
│   C# Client     │
│  (Console App)  │
└─────────────────┘
```

### Компоненты

| Компонент | Ответственность |
|-----------|----------------|
| **LED Driver** | Абстракция над NeoPixel. Фреймбуфер `width × height × 3` байт. Методы `setPixel(x, y, r, g, b)` и `show()` |
| **WASM3 Runtime** | Загрузка и исполнение .wasm программ. Экспорт host-функций, вызов `update()` на каждом тике |
| **Program Manager** | Загрузка/удаление программ из LittleFS, переключение активной программы, управление жизненным циклом |
| **Param Store** | Хранение текущих значений параметров активной программы в RAM |
| **BLE Service** | GATT-сервер: команды, ответы, загрузка WASM, уведомления |
| **LittleFS Storage** | Файловая система во flash для .wasm файлов |

---

## WASM Program API

### Host-функции (импортируются WASM-программой из модуля `env`)

```c
// Размер матрицы
int      get_width();           // Ширина (1 для одиночного LED, 32 для матрицы)
int      get_height();          // Высота (1 для одиночного LED, 32 для матрицы)

// Управление пикселями
void     set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void     draw();                // Применить изменения (flush framebuffer → LED strip)

// Чтение параметров
int32_t  get_param_i32(int param_id);
float    get_param_f32(int param_id);
```

### Экспортируемые функции (WASM → Host)

```c
void     init();                    // Вызывается один раз при загрузке программы
void     update(int32_t tick_ms);   // Вызывается каждый тик (~33ms при 30 FPS)

// Метаданные программы (указатель и длина JSON-строки в линейной памяти WASM)
int32_t  get_meta_ptr();
int32_t  get_meta_len();
```

### Формат метаданных (JSON в WASM memory)

```json
{
  "name": "Rainbow",
  "desc": "Плавная радуга",
  "params": [
    {
      "id": 0,
      "name": "Speed",
      "type": "int",
      "min": 1,
      "max": 100,
      "default": 50,
      "desc": "Скорость анимации"
    },
    {
      "id": 1,
      "name": "Brightness",
      "type": "int",
      "min": 1,
      "max": 255,
      "default": 128,
      "desc": "Яркость"
    },
    {
      "id": 2,
      "name": "Reverse",
      "type": "bool",
      "default": 0,
      "desc": "Обратное направление"
    },
    {
      "id": 3,
      "name": "Saturation",
      "type": "float",
      "min": 0.0,
      "max": 1.0,
      "default": 1.0,
      "desc": "Насыщенность"
    },
    {
      "id": 4,
      "name": "Mode",
      "type": "select",
      "options": ["Wave", "Solid", "Pulse"],
      "default": 0,
      "desc": "Режим отображения"
    }
  ]
}
```

**Типы параметров:**

| Тип | Хранение | Поля | WASM-чтение |
|-----|----------|------|-------------|
| `int` | int32 | `min`, `max`, `default` | `get_param_i32(id)` |
| `float` | float32 | `min`, `max`, `default` | `get_param_f32(id)` |
| `bool` | int32 (0/1) | `default` | `get_param_i32(id)` |
| `select` | int32 (index) | `options[]`, `default` | `get_param_i32(id)` |

### Пример WASM-программы на C (RGB Cycle)

```c
#include "wasmlеd.h"  // минимальные объявления host-функций

static const char META[] =
  "{\"name\":\"RGB Cycle\",\"desc\":\"Перебор R-G-B\","
  "\"params\":["
    "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":200,\"default\":50,\"desc\":\"Скорость\"}"
  "]}";

__attribute__((export_name("get_meta_ptr")))
int get_meta_ptr() { return (int)META; }

__attribute__((export_name("get_meta_len")))
int get_meta_len() { return sizeof(META) - 1; }

__attribute__((export_name("init")))
void init() {}

__attribute__((export_name("update")))
void update(int tick_ms) {
    int speed = get_param_i32(0);
    int w = get_width();
    int h = get_height();

    int phase = (tick_ms * speed / 1000) % 3;
    uint8_t r = (phase == 0) ? 255 : 0;
    uint8_t g = (phase == 1) ? 255 : 0;
    uint8_t b = (phase == 2) ? 255 : 0;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            set_pixel(x, y, r, g, b);
    draw();
}
```

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

### Формат Response (чанкированный)

Каждый NOTIFY-пакет:
```
[seq: uint8][flags: uint8][payload]
```
- `seq` — номер чанка (0, 1, 2, ...)
- `flags`: bit 0 = последний чанк (FINAL), bit 1 = ошибка (ERROR)
- `payload` — данные (до MTU-2 байт)

Клиент собирает чанки до получения пакета с флагом FINAL, склеивает payload.

### Команды (Command characteristic)

| Код | Команда | Payload | Ответ |
|-----|---------|---------|-------|
| `0x01` | GET_PROGRAMS | — | JSON: `[{"id":0,"name":"RGB Cycle"},...]` |
| `0x02` | GET_PARAMS | `[program_id: uint8]` | JSON: массив параметров из мета |
| `0x03` | SET_PARAM | `[program_id: uint8, param_id: uint8, value: 4 bytes]` | `{"ok":true}` или `{"ok":false,"err":"..."}` |
| `0x04` | GET_PARAM_VALUES | `[program_id: uint8]` | JSON: `{"0":50,"1":128,...}` (id→значение) |
| `0x10` | UPLOAD_START | `[total_size: uint32_le]` | `{"ok":true}` или `{"ok":false,"err":"..."}` |
| `0x11` | UPLOAD_FINISH | — | `{"ok":true,"id":3}` — ID новой программы |
| `0x12` | DELETE_PROGRAM | `[program_id: uint8]` | `{"ok":true}` или `{"ok":false,"err":"..."}` |

### Протокол загрузки WASM

1. Клиент → `UPLOAD_START` с размером файла
2. ESP выделяет буфер (в PSRAM), отвечает OK
3. Клиент → пишет чанки в `Upload` characteristic (WRITE_NR, без ответа — быстро)
4. Клиент → `UPLOAD_FINISH`
5. ESP валидирует WASM (пробует загрузить в WASM3), извлекает метаданные
6. Если ОК — сохраняет в LittleFS, отвечает с ID новой программы
7. Если ошибка — отвечает с описанием ошибки

---

## Хранение (LittleFS)

```
/
├── programs/
│   ├── 0.wasm          # Программа с ID=0
│   ├── 1.wasm          # Программа с ID=1
│   └── 2.wasm          # ...
└── config.json          # Настройки: активная программа, значения параметров
```

### config.json

```json
{
  "active": 0,
  "params": {
    "0": {"0": 50, "1": 128},
    "1": {"0": 30},
    "2": {}
  }
}
```

При старте ESP:
1. Монтирует LittleFS
2. Читает `config.json`
3. Загружает все .wasm из `programs/`, извлекает метаданные
4. Запускает активную программу с сохранёнными параметрами

---

## Структура прошивки

```
esp32-led-test/
├── firmware/
│   ├── esp32-led-test.ino          # Главный скетч: setup(), loop(), FreeRTOS tasks
│   ├── led_driver.h / .cpp         # LED Driver: фреймбуфер, NeoPixel
│   ├── wasm_engine.h / .cpp        # WASM3 обёртка: загрузка, host-функции, tick
│   ├── program_manager.h / .cpp    # Управление программами: load, switch, delete
│   ├── ble_service.h / .cpp        # BLE GATT сервер
│   ├── storage.h / .cpp            # LittleFS + config
│   └── param_store.h / .cpp        # Параметры программ в RAM
├── programs/                        # Исходники WASM-программ (C)
│   ├── common/
│   │   └── api.h                   # Объявления host-функций
│   ├── rgb_cycle/
│   │   └── main.c
│   ├── rainbow/
│   │   └── main.c
│   └── random_blink/
│       └── main.c
├── client/
│   ├── WasmLedClient.csproj
│   └── Program.cs
├── tools/
│   └── build_wasm.bat              # Скрипт сборки .wasm из C (clang --target=wasm32)
├── SPEC.md
└── README.md
```

---

## FreeRTOS задачи

| Задача | Core | Приоритет | Описание |
|--------|------|-----------|----------|
| `renderTask` | 1 | 2 | Основной цикл: вызывает `wasm_update()` каждые ~33ms (30 FPS) |
| `bleTask` | 0 | 1 | Обработка BLE-событий (Arduino BLE работает на Core 0 по умолчанию) |

`loop()` остаётся пустым или используется для watchdog.

---

## C# клиент

### Функциональность

- Сканирование BLE, подключение к устройству по Service UUID
- Главное меню:
  ```
  === WasmLED Controller ===
  Connected to: WasmLED (XX:XX:XX:XX:XX:XX)

  Programs:
    [*] 0: RGB Cycle
    [ ] 1: Rainbow
    [ ] 2: Random Blink

  [1-9] Select program  [P] Parameters  [U] Upload  [D] Delete  [Q] Quit
  ```
- Меню параметров:
  ```
  === RGB Cycle — Parameters ===

  0: Speed     = 50    (int, 1..200)
  1: Brightness = 128  (int, 1..255)

  Enter: <id> <value>  |  [B] Back
  > 0 80
  Speed set to 80
  ```
- Загрузка WASM: указать путь к .wasm файлу, клиент отправляет по BLE

### Зависимости

- .NET 9.0, `net9.0-windows10.0.19041.0`
- `Windows.Devices.Bluetooth` (Windows SDK)
- Без внешних NuGet-пакетов

---

## Три встроенные программы

### 1. RGB Cycle
Перебор чистых цветов: красный → зелёный → синий → красный.

**Параметры:**
| ID | Имя | Тип | Диапазон | Default | Описание |
|----|-----|-----|----------|---------|----------|
| 0 | Speed | int | 1–200 | 50 | Скорость перебора |
| 1 | Brightness | int | 1–255 | 255 | Яркость |

### 2. Rainbow
Плавная HSV-радуга, hue вращается по кругу.

**Параметры:**
| ID | Имя | Тип | Диапазон | Default | Описание |
|----|-----|-----|----------|---------|----------|
| 0 | Speed | int | 1–100 | 30 | Скорость вращения |
| 1 | Brightness | int | 1–255 | 128 | Яркость |
| 2 | Saturation | float | 0.0–1.0 | 1.0 | Насыщенность |

### 3. Random Blink
Случайные цветные вспышки.

**Параметры:**
| ID | Имя | Тип | Диапазон | Default | Описание |
|----|-----|-----|----------|---------|----------|
| 0 | Speed | int | 1–100 | 50 | Частота вспышек |
| 1 | Brightness | int | 1–255 | 200 | Яркость |
| 2 | Fade | bool | — | 1 | Плавное затухание |

---

## Сборка

### Прошивка

```bash
# Установить arduino-cli, добавить ESP32 board package
arduino-cli core install esp32:esp32

# Компиляция
arduino-cli compile --fqbn esp32:esp32:esp32s3 --build-property "build.partitions=default_16MB" firmware/

# Прошивка
arduino-cli upload --fqbn esp32:esp32:esp32s3 --port COM3 firmware/
```

### WASM-программы

Требуется: [WASI SDK](https://github.com/WebAssembly/wasi-sdk) или clang с wasm32 target.

```bash
clang --target=wasm32 -nostdlib -O2 \
  -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined \
  -o rgb_cycle.wasm programs/rgb_cycle/main.c
```

### C# клиент

```bash
cd client
dotnet build -r win-x64
dotnet run
```

---

## Ограничения и допущения (MVP)

- Максимум 16 программ одновременно
- Максимальный размер .wasm файла: 64 KB
- WASM memory: 1 page (64 KB) — достаточно для MVP
- Параметры хранятся в RAM, персистятся в config.json при изменении
- BLE MTU: минимум 20 байт (работает с любым клиентом), оптимально 512
- Один клиент одновременно (BLE single connection)
- WASM3 runtime в PSRAM для экономии основной RAM
