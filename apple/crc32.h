#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void preload_crc();
uint32_t compute_crc_32(unsigned char *buffer, unsigned long length);

#ifdef __cplusplus
};
#endif
