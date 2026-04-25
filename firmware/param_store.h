#ifndef PARAM_STORE_H
#define PARAM_STORE_H

#include <Arduino.h>

#define MAX_PARAMS 16

class ParamStore {
public:
    ParamStore();

    void    setInt(uint8_t id, int32_t val);
    int32_t getInt(uint8_t id) const;

    void    setFloat(uint8_t id, float val);
    float   getFloat(uint8_t id) const;

    void    reset();

    String  toJson() const;
    void    fromJson(const char* json);

private:
    // Union allows storing int32 and float in the same 4 bytes
    union ParamValue {
        int32_t i;
        float   f;
    };

    ParamValue _values[MAX_PARAMS];
    bool       _set[MAX_PARAMS];     // track which params have been explicitly set
    bool       _isFloat[MAX_PARAMS]; // track type for correct JSON serialization
};

#endif // PARAM_STORE_H
