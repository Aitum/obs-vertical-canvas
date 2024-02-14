
#include <obs-module.h>
#include "multi-canvas-source.h"

struct multi_canvas_info {
	obs_source_t *source;
	uint32_t width;
	uint32_t height;
	DARRAY(obs_view_t *) views;
	DARRAY(uint32_t) widths;
	DARRAY(uint32_t) heights;
};

const char *multi_canvas_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "vertical_multi_canvas";
}

void *multi_canvas_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct multi_canvas_info *multi_canvas =
		bzalloc(sizeof(struct multi_canvas_info));
	multi_canvas->source = source;
	return multi_canvas;
}

void multi_canvas_destroy(void *data)
{
	struct multi_canvas_info *mc = data;
	da_free(mc->views);
	da_free(mc->widths);
	da_free(mc->heights);
	bfree(data);
}

bool view_get_width(void *data, struct obs_video_info *ovi)
{
	uint32_t *width = data;
	if (ovi->base_width > *width)
		*width = ovi->base_width;
	return true;
}

bool view_get_height(void *data, struct obs_video_info *ovi)
{
	uint32_t *height = data;
	if (ovi->base_height > *height)
		*height = ovi->base_height;
	return true;
}

static void multi_canvas_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct multi_canvas_info *mc = data;
	gs_matrix_push();
	for (uint32_t i = 0; i < MAX_CHANNELS; i++) {
		obs_source_t *s = obs_get_output_source(i);
		if (!s)
			continue;
		if (obs_source_removed(s)) {
			obs_source_release(s);
			continue;
		}
		obs_source_video_render(s);
		obs_source_release(s);
	}

	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	gs_matrix_translate3f((float)ovi.base_width, 0.0f, 0.0f);

	for (size_t i = 0; i < mc->views.num; i++) {
		obs_view_t *view = mc->views.array[i];
		obs_view_render(view);
		gs_matrix_translate3f((float)mc->widths.array[i], 0.0f, 0.0f);
	}
	gs_matrix_pop();
}

uint32_t multi_canvas_get_width(void *data)
{
	struct multi_canvas_info *mc = data;
	return mc->width;
}

uint32_t multi_canvas_get_height(void *data)
{
	struct multi_canvas_info *mc = data;
	return mc->height;
}

void multi_canvas_update_size(struct multi_canvas_info *mc)
{
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	uint32_t width = ovi.base_width;
	uint32_t height = ovi.base_height;

	for (size_t i = 0; i < mc->widths.num; i++) {
		width += mc->widths.array[i];
		if (mc->heights.array[i] > height)
			height = mc->heights.array[i];
	}
	mc->width = width;
	mc->height = height;
}

void multi_canvas_source_add_view(void *data, obs_view_t *view, uint32_t width,
				  uint32_t height)
{
	struct multi_canvas_info *mc = data;
	for (size_t i = 0; i < mc->views.num; i++) {
		if (mc->views.array[i] == view)
			return;
	}
	da_push_back(mc->widths, &width);
	da_push_back(mc->heights, &height);
	da_push_back(mc->views, &view);
	multi_canvas_update_size(mc);
}

void multi_canvas_source_remove_view(void *data, obs_view_t *view)
{
	struct multi_canvas_info *mc = data;
	for (size_t i = 0; i < mc->views.num; i++) {
		if (mc->views.array[i] == view) {
			da_erase(mc->views, i);
			da_erase(mc->widths, i);
			da_erase(mc->heights, i);
			break;
		}
	}
	multi_canvas_update_size(mc);
}

struct obs_source_info multi_canvas_source = {
	.id = "vertical_multi_canvas_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED,
	.get_name = multi_canvas_get_name,
	.create = multi_canvas_create,
	.destroy = multi_canvas_destroy,
	.video_render = multi_canvas_video_render,
	.get_width = multi_canvas_get_width,
	.get_height = multi_canvas_get_height,
};
