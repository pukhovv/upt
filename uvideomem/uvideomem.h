#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define UVIDEOMEM_W (1560u)
#define UVIDEOMEM_H (1024u)
int uvideomem_init(void (*render)(void *, unsigned int, unsigned int), void *context);
bool uvideomem_present(uint32_t const *row, unsigned int y, unsigned long long frame);

#ifdef __cplusplus
}
#endif
