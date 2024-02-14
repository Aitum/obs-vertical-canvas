#pragma once

#include <util/darray.h>

#ifdef __cplusplus
extern "C" {
#endif

void multi_canvas_source_add_view(void *data, obs_view_t *view, uint32_t width,
				  uint32_t height);
void multi_canvas_source_remove_view(void *data, obs_view_t *view);

extern struct obs_source_info multi_canvas_source;

#ifdef __cplusplus
};
#endif
