export const SERVICE_UUID = '0000ff00-0000-1000-8000-00805f9b34fb';

export const CHAR = {
  COMMAND:        '0000ff01-0000-1000-8000-00805f9b34fb',
  RESPONSE:       '0000ff02-0000-1000-8000-00805f9b34fb',
  ACTIVE_PROGRAM: '0000ff03-0000-1000-8000-00805f9b34fb',
  UPLOAD:         '0000ff04-0000-1000-8000-00805f9b34fb',
  PARAM_VALUES:   '0000ff05-0000-1000-8000-00805f9b34fb',
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
} as const;

export const FLAG_FINAL = 0x01;
export const FLAG_ERROR = 0x02;

export const UPLOAD_CHUNK_SIZE = 200;
export const RESPONSE_TIMEOUT_MS = 5000;
export const CONNECT_TIMEOUT_MS = 10000;
