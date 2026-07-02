#ifndef VERSION_H
#define VERSION_H

// Firmware build number. Increment by 1 on every build you flash/ship, so
// clients (and OTA) can tell exactly what's running and confirm an update took.
// Exposed over BLE in the GET_HW_CONFIG response as the "build" field.
#define FW_BUILD 15

#endif // VERSION_H
