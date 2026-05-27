#ifndef LV2_CORE_LV2_H
#define LV2_CORE_LV2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* LV2_Handle;

typedef struct LV2_Feature {
  const char* URI;
  const void* data;
} LV2_Feature;

typedef struct LV2_Descriptor {
  const char* URI;
  LV2_Handle (*instantiate)(const struct LV2_Descriptor* descriptor,
                            double rate,
                            const char* bundle_path,
                            const LV2_Feature* const* features);
  void (*connect_port)(LV2_Handle instance, uint32_t port, void* data);
  void (*activate)(LV2_Handle instance);
  void (*run)(LV2_Handle instance, uint32_t sample_count);
  void (*deactivate)(LV2_Handle instance);
  void (*cleanup)(LV2_Handle instance);
  const void* (*extension_data)(const char* uri);
} LV2_Descriptor;

#ifndef LV2_SYMBOL_EXPORT
#define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif
