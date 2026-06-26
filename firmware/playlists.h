#ifndef PLAYLISTS_H
#define PLAYLISTS_H

#include <Arduino.h>
#include <vector>

// Playlists owned by the firmware. One JSON file per playlist:
//   /playlists/{id}.json = {"name","mode","interval","positions":[ {pos}, ... ]}
// A position is opaque to the firmware (the app reads/applies it):
//   {"prog":<id>,"slug":"...","name":"...","params":[{"id","value","f"}]}
// Order == array order. Positions are addressed by their array index.
// mode: 0=off, 1=next, 2=random.

namespace Playlists {

    // JSON summary array: [{"id","name","mode","interval","count"}, ...]
    String listJson();

    // Full raw JSON of a playlist file (""=not found). Sent via chunked response.
    String getJson(uint8_t id);

    // Create an empty playlist, returns new id or -1.
    int create(const String& name);

    bool rename(uint8_t id, const String& name);
    bool remove(uint8_t id);
    bool setRotation(uint8_t id, uint8_t mode, uint16_t interval);

    // Append a position (raw JSON object). Returns new index or -1.
    int addPosition(uint8_t id, const String& posJson);

    // Remove the position at the given array index.
    bool removePosition(uint8_t id, uint8_t index);

    // Reorder positions: indicesJson is a JSON array of current indices in the
    // new order, e.g. [2,0,1].
    bool reorder(uint8_t id, const String& indicesJson);

} // namespace Playlists

#endif // PLAYLISTS_H
