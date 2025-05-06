#pragma once

#include <util/darray.h>

#ifdef __cplusplus
extern "C" {
#endif

void multi_canvas_source_add_canvas(void *data, obs_canvas_t *canvas, uint32_t width, uint32_t height);
void multi_canvas_source_remove_canvas(void *data, obs_canvas_t *canvas);

extern struct obs_source_info multi_canvas_source;

#ifdef __cplusplus
};
#endif
