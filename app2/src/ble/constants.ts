export const SERVICE_UUID = '0000ff00-0000-1000-8000-00805f9b34fb';

export const CHAR = {
  COMMAND:        '0000ff01-0000-1000-8000-00805f9b34fb',
  RESPONSE:       '0000ff02-0000-1000-8000-00805f9b34fb',
  ACTIVE_PROGRAM: '0000ff03-0000-1000-8000-00805f9b34fb',
  UPLOAD:         '0000ff04-0000-1000-8000-00805f9b34fb',
  PARAM_VALUES:   '0000ff05-0000-1000-8000-00805f9b34fb',
  EVENTS:         '0000ff06-0000-1000-8000-00805f9b34fb',
} as const;

export const EVT = {
  PROGRAM_ADDED:  0x01,
  PROGRAM_DELETED: 0x02,
  PL_ADVANCE:     0x03,  // lamp auto-advanced a playlist; second byte = new index
  PL_STOPPED:     0x04,  // lamp left playlist mode (manual/touch program switch)
} as const;

export const CMD = {
  GET_PROGRAMS:    0x01,
  GET_PARAMS:      0x02,
  SET_PARAM:       0x03,
  GET_PARAM_VALUES:0x04,
  UPLOAD_START:    0x10,
  UPLOAD_FINISH:   0x11,
  DELETE_PROGRAM:  0x12,
  SET_NAME:        0x20,
  GET_NAME:        0x21,
  GET_HW_CONFIG:   0x22,
  SET_HW_CONFIG:   0x23,
  REBOOT:          0x24,
  GET_META:        0x25,
  SET_META:        0x26,
  GET_POWER:       0x27,
  SET_POWER:       0x28,
  GET_STORAGE:     0x29,
  SET_ORDER:       0x2A,
  GET_ORDER:       0x2B,
  CLEAR_STORAGE:   0x2C,
  GET_FILE:        0x2D,
  LIST_FILES:      0x2E,
  WRITE_FILE:      0x2F,
  DELETE_FILE:     0x30,
  APPEND_FILE:     0x31,
  PL_LIST:         0x32,
  PL_GET:          0x33,
  PL_CREATE:       0x34,
  PL_RENAME:       0x35,
  PL_DELETE:       0x36,
  PL_SET_ROTATION: 0x37,
  PL_ADD_POS:      0x38,
  PL_DEL_POS:      0x39,
  PL_REORDER:      0x3A,
  APPLY_POS:       0x3B,
  PL_PLAY:         0x3C,
  PL_STOP:         0x3D,
  PL_STATE:        0x3E,
} as const;

// Upload pipeline payload types (UPLOAD_START byte after the 4-byte size)
export const UPLOAD_TYPE = {
  WASM:     0,
  META:     1,
  FIRMWARE: 2,
} as const;

export const FLAG_FINAL = 0x01;
export const FLAG_ERROR = 0x02;

export const UPLOAD_CHUNK_SIZE = 200;
export const RESPONSE_TIMEOUT_MS = 5000;
export const CONNECT_TIMEOUT_MS = 10000;
