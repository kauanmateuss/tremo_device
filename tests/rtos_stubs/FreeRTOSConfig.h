#include "../../include/FreeRTOSConfig.h"
/* O simulador usa pilhas e ponteiros do host. */
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE (256 * 1024)
