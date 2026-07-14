#ifndef DUAL_CLAMP_H
#define DUAL_CLAMP_H
#include <stdint.h>

// Clamp a Pool A share percentage into [0, 100].
uint8_t dual_clamp_ratio(int32_t v);

// Clamp a slice interval (ms) into [100, 60000].
uint16_t dual_clamp_interval(int32_t v);

#endif // DUAL_CLAMP_H
