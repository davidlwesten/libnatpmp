#pragma once

#include <stddef.h>
#include <stdint.h>

// Buffers as the wire formats read and write them

typedef struct const_view {
  size_t size;
  uint8_t const *data;
} const_view_t;

typedef struct view {
  size_t size;
  uint8_t *data;
} view_t;
