#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int decode_cover(const unsigned char* bytes, size_t length, uint32_t* pixels, unsigned edge);
#ifdef __cplusplus
}
#endif
