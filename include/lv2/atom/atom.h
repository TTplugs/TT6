#ifndef LV2_ATOM_ATOM_H
#define LV2_ATOM_ATOM_H

#include <stdint.h>
#include "../urid/urid.h"

typedef struct {
  uint32_t size;
  LV2_URID type;
} LV2_Atom;

typedef struct {
  int64_t frames;
} LV2_Atom_Event_Time;

typedef struct {
  LV2_Atom_Event_Time time;
  LV2_Atom body;
} LV2_Atom_Event;

typedef struct {
  LV2_Atom atom;
  uint8_t body[];
} LV2_Atom_Sequence;

#endif
