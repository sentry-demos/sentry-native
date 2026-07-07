// minimp4 implementation lives in its own translation unit: it defines
// internal symbols (bs_t et al.) that collide with minih264e's when the two
// headers share a TU.
#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
