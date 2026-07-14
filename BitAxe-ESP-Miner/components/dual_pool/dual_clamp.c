#include "dual_clamp.h"

uint8_t dual_clamp_ratio(int32_t v)
{
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

uint16_t dual_clamp_interval(int32_t v)
{
    if (v < 100)   return 100;
    if (v > 60000) return 60000;
    return (uint16_t)v;
}
