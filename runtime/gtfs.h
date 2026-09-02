#pragma once
#include <stdint.h>
#include <stddef.h>
int gtfs_init(const char *iso_path);
const char* gtfs_find(const char *name, uint32_t *out_off, uint32_t *out_size);
size_t gtfs_read(const char *name, uint8_t **out);
