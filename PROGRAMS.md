# Shades Lamp — Program Registry & Metadata Spec

## Принцип

**Лампа — единственный source of truth.** Телефон всегда получает метаданные программ из лампы, а не из маркетплейса. Если программу залили по USB, CLI или из другого клиента — телефон всё равно покажет обложку, автора, категорию и локализованные строки.

---

## Три слоя данных

```
┌─────────────────────────────────────────────────────┐
│  1. WASM-embedded meta (get_meta_ptr/get_meta_len)  │  ← runtime: name, params
│     Встроен в бинарник, читается firmware            │
├─────────────────────────────────────────────────────┤
│  2. meta.json на LittleFS (/meta/{id}.json)         │  ← rich UI: cover, author,
│     Хранится рядом с .wasm, отдаётся по BLE         │     category, i18n, about
├─────────────────────────────────────────────────────┤
│  3. GitHub registry (index.json + program.json)     │  ← marketplace: ratings,
│     Только для каталога и скачивания                 │     downloads, featured
└─────────────────────────────────────────────────────┘
```

### Что где живёт

| Данные | WASM meta | meta.json (лампа) | GitHub registry |
|--------|-----------|-------------------|-----------------|
| name | `✓` (fallback) | `✓` (+ i18n) | `✓` |
| desc | `✓` (fallback) | `✓` (+ i18n) | `✓` |
| params (schema) | `✓` | — | `✓` |
| param names/desc | `✓` (en) | `✓` (i18n) | `✓` (i18n) |
| cover gradient | — | `✓` | `✓` |
| pulse color | — | `✓` | `✓` |
| author | — | `✓` | `✓` |
| category | — | `✓` | `✓` |
| tags | — | `✓` | `✓` |
| about (long text) | — | `✓` (i18n) | `✓` (i18n) |
| version | — | `✓` | `✓` |
| rating / downloads | — | — | `✓` |
| featured | — | — | `✓` |
| sha256 | — | — | `✓` |

---

## LittleFS на лампе

```
/programs/
  0.wasm          ← WASM binary
  1.wasm
  ...
/meta/
  0.json          ← Rich metadata (cover, author, i18n...)
  1.json
  ...
/params/
  0.json          ← Runtime param values {"0": 50, "1": 128}
  1.json
  ...
/config.json      ← Global device config
```

### Откуда берётся meta.json

| Сценарий | Кто создаёт meta.json |
|----------|----------------------|
| Установка из маркетплейса | Приложение скачивает program.json, вырезает `stats`/`featured`/`sha256`, отправляет остальное как meta.json через BLE |
| Сideload через CLI | CLI генерирует минимальный meta.json из WASM-метаданных + дефолтные cover/category |
| WASM без meta.json | Firmware создаёт fallback из WASM-embedded meta: `{"name": "...", "desc": "...", "author": "unknown"}` |

---

## meta.json — формат (хранится на лампе)

Максимум **2 КБ** (ограничение BLE transfer time, не flash). При 128 слотах × 2 КБ = 256 КБ на flash — при 12 МБ LittleFS это ~2%.

```json
{
  "name": "Aurora Drift",
  "desc": "Northern lights with slow drift",
  "author": "k.morov",
  "category": "Ambient",
  "version": "1.2.0",

  "cover": {
    "from": "#0B3D2E",
    "to": "#7B2CBF",
    "via": "#06B6D4",
    "angle": 200
  },
  "pulse": "#06B6D4",

  "about": "Layered sine waves create natural-looking curtain effects.",

  "tags": ["aurora", "ambient", "sine"],

  "i18n": {
    "ru": {
      "name": "Северное сияние",
      "desc": "Медленный дрейф полярного сияния",
      "about": "Многослойные синусоиды создают эффект колышущихся занавесей.",
      "params": {
        "0": { "name": "Скорость", "desc": "Скорость дрейфа" },
        "1": { "name": "Яркость", "desc": "Общая яркость" },
        "2": { "name": "Сдвиг оттенка", "desc": "Смещение базового оттенка" },
        "3": { "name": "Плотность", "desc": "Плотность лент" },
        "4": { "name": "Палитра", "desc": "Цветовая палитра", "options": ["Северное сияние", "Солнечная вспышка", "Глубокий океан"] }
      }
    }
  }
}
```

### Поля meta.json

| Поле | Тип | Обязат. | Описание |
|------|-----|---------|----------|
| `name` | string | да | Имя (default locale = en) |
| `desc` | string | да | Краткое описание |
| `author` | string | да | Автор. `"built-in"` для встроенных |
| `category` | enum | да | `"Effects"` / `"Ambient"` / `"Visualizers"` / `"Games"` |
| `version` | string | нет | Версия `"1.0.0"` |
| `cover` | Cover | да | Градиент обложки |
| `pulse` | string | да | Доминантный цвет (hex) |
| `about` | string | нет | Развёрнутое описание |
| `tags` | string[] | нет | Теги |
| `i18n` | object | нет | Локализации (см. ниже) |

### Cover

| Поле | Тип | Обязат. | Описание |
|------|-----|---------|----------|
| `from` | string | да | Начальный цвет (hex) |
| `to` | string | да | Конечный цвет (hex) |
| `via` | string | нет | Промежуточный цвет |
| `angle` | number | да | Угол градиента (0–360) |

### i18n — локализация

Ключ — ISO 639-1 код языка (`ru`, `de`, `ja`, `zh`, ...). Значение — объект с переопределёнными строками. Непереведённые поля берутся из корня (fallback на en).

```json
{
  "i18n": {
    "ru": {
      "name": "Северное сияние",
      "desc": "Медленный дрейф полярного сияния",
      "about": "...",
      "params": {
        "0": { "name": "Скорость", "desc": "Скорость дрейфа" },
        "4": {
          "name": "Палитра",
          "desc": "Цветовая палитра",
          "options": ["Северное сияние", "Солнечная вспышка", "Глубокий океан"]
        }
      }
    },
    "de": {
      "name": "Polarlicht",
      "desc": "Langsames Nordlicht-Drift"
    }
  }
}
```

**Правила:**
- `i18n.{lang}` переопределяет только указанные поля
- `i18n.{lang}.params.{id}` переопределяет name/desc/options конкретного параметра
- Для `select` параметров: `options` массив должен быть той же длины, что в WASM-meta
- Если локаль не найдена → fallback на корневые поля (en)
- Приложение определяет язык через `Localization.locale` (React Native)

### Резолвинг строки в приложении

```typescript
function resolve(meta: Meta, lang: string): ResolvedMeta {
  const loc = meta.i18n?.[lang];
  return {
    name:  loc?.name  ?? meta.name,
    desc:  loc?.desc  ?? meta.desc,
    about: loc?.about ?? meta.about,
    // ...
  };
}

function resolveParam(meta: Meta, lang: string, param: Param): ResolvedParam {
  const loc = meta.i18n?.[lang]?.params?.[param.id];
  return {
    ...param,
    name:    loc?.name    ?? param.name,
    desc:    loc?.desc    ?? param.desc,
    options: loc?.options ?? param.options,
  };
}
```

---

## BLE-протокол — новые команды

### CMD_GET_META (0x25)

Получить meta.json программы с лампы.

| | Описание |
|--|----------|
| **Request** | `[0x25, programId:1]` |
| **Response** | JSON содержимое `/meta/{id}.json` |
| **Fallback** | Если файла нет → firmware генерирует минимальный JSON из WASM-meta |

### CMD_SET_META (0x26)

Записать meta.json на лампу (при установке из маркетплейса).

| | Описание |
|--|----------|
| **Request** | `[0x26, programId:1]` + chunked JSON на ff04 (как WASM upload) |
| **Response** | `{"ok": true}` |
| **Лимит** | 2048 байт |

### Обновлённый CMD_GET_PROGRAMS (0x01)

Расширить ответ, включив данные из meta.json:

**Было:**
```json
[{"id": 0, "name": "Rainbow"}]
```

**Стало:**
```json
[
  {
    "id": 0,
    "name": "Rainbow",
    "author": "built-in",
    "category": "Ambient",
    "cover": {"from": "#FF6B6B", "to": "#FFD93D", "via": "#6BCB77", "angle": 95},
    "pulse": "#FFD93D"
  }
]
```

Firmware мержит WASM-meta (`name`) и `/meta/{id}.json` (`author`, `cover`, `pulse`, `category`). Это позволяет отрисовать Library экран одним запросом. Полные данные (i18n, about, tags) — через CMD_GET_META.

---

## GitHub registry — index.json

Маркетплейс-каталог. Скачивается приложением, **не хранится на лампе**.

```json
{
  "version": 1,
  "updated": "2026-04-27T12:00:00Z",
  "programs": [
    {
      "slug": "aurora",
      "name": "Aurora Drift",
      "desc": "Northern lights with slow drift",
      "author": "k.morov",
      "category": "Ambient",
      "cover": {"from": "#0B3D2E", "to": "#7B2CBF", "via": "#06B6D4", "angle": 200},
      "pulse": "#06B6D4",
      "version": "1.2.0",
      "size": 2948,
      "paramCount": 5,
      "rating": 4.8,
      "downloads": 3200,
      "featured": false,
      "tags": ["aurora", "ambient"]
    }
  ]
}
```

## GitHub registry — program.json

Полные данные программы. Хранится в `programs/{slug}/program.json`.

```json
{
  "slug": "aurora",
  "name": "Aurora Drift",
  "desc": "Northern lights with slow drift",
  "author": "k.morov",
  "category": "Ambient",
  "version": "1.2.0",
  "license": "MIT",

  "cover": {"from": "#0B3D2E", "to": "#7B2CBF", "via": "#06B6D4", "angle": 200},
  "pulse": "#06B6D4",

  "wasm": {
    "file": "main.wasm",
    "size": 2948,
    "sha256": "a1b2c3d4e5f6..."
  },

  "params": [
    {"id": 0, "name": "Speed", "type": "int", "min": 1, "max": 60, "default": 12, "desc": "Drift speed"},
    {"id": 1, "name": "Brightness", "type": "int", "min": 1, "max": 255, "default": 160, "desc": "Brightness"},
    {"id": 2, "name": "Hue Shift", "type": "float", "min": 0.0, "max": 1.0, "default": 0.5, "desc": "Hue offset"},
    {"id": 3, "name": "Density", "type": "float", "min": 0.0, "max": 1.0, "default": 0.7, "desc": "Ribbon density"},
    {"id": 4, "name": "Palette", "type": "select", "options": ["Northern Lights", "Solar Flare", "Deep Ocean"], "default": 0, "desc": "Color palette"}
  ],

  "about": "Layered sine waves create natural-looking aurora curtain effects.",

  "technical": {
    "target": "wasm32-unknown",
    "memory": "1 page (64 KB)",
    "imports": ["env.set_pixel", "env.draw", "env.get_width", "env.get_height", "env.get_param_i32", "env.get_param_f32"],
    "exports": ["init", "update", "get_meta_ptr", "get_meta_len"]
  },

  "stats": {
    "rating": 4.8,
    "downloads": 3200,
    "firstPublished": "2026-03-15",
    "lastUpdated": "2026-04-10"
  },

  "tags": ["aurora", "northern-lights", "ambient", "sine"],
  "featured": false,

  "i18n": {
    "ru": {
      "name": "Северное сияние",
      "desc": "Медленный дрейф полярного сияния",
      "about": "Многослойные синусоиды создают эффект колышущихся занавесей.",
      "params": {
        "0": {"name": "Скорость", "desc": "Скорость дрейфа"},
        "1": {"name": "Яркость", "desc": "Общая яркость"},
        "2": {"name": "Сдвиг оттенка", "desc": "Смещение базового оттенка"},
        "3": {"name": "Плотность", "desc": "Плотность лент"},
        "4": {"name": "Палитра", "desc": "Цветовая палитра", "options": ["Северное сияние", "Солнечная вспышка", "Глубокий океан"]}
      }
    }
  }
}
```

---

## Потоки данных

### Установка из маркетплейса

```
App                        GitHub                 Lamp
 │                           │                      │
 │── GET index.json ────────►│                      │
 │◄── каталог ──────────────│                      │
 │                           │                      │
 │  [user taps Install]      │                      │
 │── GET programs/aurora/ ──►│                      │
 │◄── program.json + .wasm ─│                      │
 │                           │                      │
 │  program.json                                    │
 │    → убрать stats, featured, sha256, wasm        │
 │    → оставить name,desc,author,category,         │
 │      cover,pulse,version,about,tags,i18n         │
 │    = meta.json                                   │
 │                                                  │
 │── CMD_UPLOAD_START ─────────────────────────────►│
 │── WASM chunks (ff04) ──────────────────────────►│
 │── CMD_UPLOAD_FINISH ────────────────────────────►│
 │◄── {"ok":true, "id": 7} ───────────────────────│
 │                                                  │
 │── CMD_SET_META(7, meta.json) ──────────────────►│  → /meta/7.json
 │◄── {"ok":true} ────────────────────────────────│
```

### Подключение телефона к лампе

```
App                                    Lamp
 │                                       │
 │── CMD_GET_PROGRAMS ──────────────────►│
 │◄── [{id,name,author,category,        │  ← firmware мержит
 │      cover,pulse}, ...] ─────────────│     WASM-meta + /meta/*.json
 │                                       │
 │  [user opens program detail]          │
 │── CMD_GET_META(id) ─────────────────►│
 │◄── full meta.json (with i18n) ──────│
 │                                       │
 │── CMD_GET_PARAMS(id) ───────────────►│
 │◄── params schema from WASM ─────────│
 │                                       │
 │── CMD_GET_PARAM_VALUES(id) ─────────►│
 │◄── {"0":50, "1":128} ──────────────│
```

### Sideload (CLI / USB)

```
CLI                                    Lamp
 │                                       │
 │── upload main.wasm ─────────────────►│  → /programs/7.wasm
 │                                       │
 │  (no meta.json provided)             │
 │                                       │  firmware auto-generates:
 │                                       │  /meta/7.json = {
 │                                       │    "name": <from WASM>,
 │                                       │    "desc": <from WASM>,
 │                                       │    "author": "unknown",
 │                                       │    "category": "Effects",
 │                                       │    "cover": {"from":"#666","to":"#999","angle":135},
 │                                       │    "pulse": "#888"
 │                                       │  }
```

---

## Fallback-генерация meta.json на firmware

Когда WASM загружен без meta.json, firmware создаёт минимальный:

```cpp
String generateFallbackMeta(const String& wasmMeta) {
    JsonDocument doc, out;
    deserializeJson(doc, wasmMeta);

    out["name"]     = doc["name"] | "Untitled";
    out["desc"]     = doc["desc"] | "";
    out["author"]   = "unknown";
    out["category"] = "Effects";

    JsonObject cover = out["cover"].to<JsonObject>();
    cover["from"]  = "#555555";
    cover["to"]    = "#999999";
    cover["angle"] = 135;

    out["pulse"] = "#888888";

    String result;
    serializeJson(out, result);
    return result;
}
```

---

## Бюджет размера

| Что | Лимит | Обоснование |
|-----|-------|-------------|
| WASM-embedded meta | 4 КБ | Текущий hardcoded лимит в firmware |
| meta.json на LittleFS | 2 КБ | BLE transfer + flash экономия |
| index.json (весь каталог) | ~100 КБ | Одноразовая загрузка по HTTP |
| program.json (registry) | без лимита | Только на GitHub, не на лампе |

При 128 программах × 2 КБ meta = 256 КБ на LittleFS. Flash 16 МБ (~12 МБ под LittleFS) — запас огромный.

---

## Локализация — поддерживаемые языки

| Код | Язык | Приоритет |
|-----|------|-----------|
| (root) | English | default / fallback |
| `ru` | Русский | v1.0 |
| `de` | Deutsch | v1.1+ |
| `ja` | 日本語 | v1.1+ |
| `zh` | 中文 | v1.1+ |

Авторы программ могут добавлять любые локали в `i18n`. Приложение показывает строки для текущей системной локали, с fallback на корневые (en).

---

## Скрипт генерации index.json

```bash
jq -s '{
  version: 1,
  updated: (now | strftime("%Y-%m-%dT%H:%M:%SZ")),
  programs: [.[] | {
    slug, name, desc, author, category,
    cover, pulse, version,
    size: .wasm.size,
    paramCount: (.params | length),
    rating: .stats.rating,
    downloads: .stats.downloads,
    featured: (.featured // false),
    tags: (.tags // [])
  }]
}' programs/*/program.json > index.json
```

## Workflow: публикация

```
1. Автор пишет main.c → компилирует main.wasm
2. Создаёт program.json (meta + params + cover + i18n)
3. PR в github.com/shades-lamp/registry
4. CI проверяет:
   - JSON валиден по схеме
   - main.wasm загружается в wasm3
   - sha256 совпадает
   - slug уникален
   - i18n.*.params.*.options длины совпадают с params[].options
5. Мерж → index.json автоматически пересобирается
6. Приложение подтягивает обновлённый index.json
```
