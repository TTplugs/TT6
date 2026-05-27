#ifndef LV2_ATOM_UTIL_H
#define LV2_ATOM_UTIL_H

#include "atom.h"

#define LV2_ATOM_SEQUENCE_FOREACH(seq, iter) \
  for (LV2_Atom_Event* iter = (LV2_Atom_Event*)((uint8_t*)(seq)->body + 8); \
       (uint8_t*)iter < ((uint8_t*)&(seq)->atom + sizeof(LV2_Atom) + (seq)->atom.size); \
       iter = (LV2_Atom_Event*)((uint8_t*)iter + sizeof(LV2_Atom_Event) + ((iter->body.size + 7u) & ~7u)))

#endif
