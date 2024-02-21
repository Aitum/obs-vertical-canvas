
#include <obs-module.h>
#include "multi-canvas-source.h"

struct multi_canvas_info {
	obs_source_t *source;
	uint32_t width;
	uint32_t height;
	DARRAY(obs_view_t *) views;
	DARRAY(uint32_t) widths;
	DARRAY(uint32_t) heights;
	DARRAY(gs_texrender_t *) renders;
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
	for (size_t i = 0; i < mc->renders.num; i++) {
		gs_texrender_destroy(mc->renders.array[i]);
	}
	da_free(mc->renders);
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
		gs_matrix_push();
		gs_blend_state_push();
		obs_source_video_render(s);
		gs_blend_state_pop();
		gs_matrix_pop();
		obs_source_release(s);
	}

	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	gs_matrix_translate3f((float)ovi.base_width, 0.0f, 0.0f);

	effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	for (size_t i = 0; i < mc->views.num; i++) {
		obs_view_t *view = mc->views.array[i];
		const enum gs_color_format format =
			gs_get_format_from_space(gs_get_color_space());
		if (gs_texrender_get_format(mc->renders.array[i]) != format) {
			gs_texrender_destroy(mc->renders.array[i]);
			mc->renders.array[i] =
				gs_texrender_create(format, GS_ZS_NONE);
		}

		gs_texrender_reset(mc->renders.array[i]);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (gs_texrender_begin_with_color_space(
			    mc->renders.array[i], mc->widths.array[i],
			    mc->heights.array[i], gs_get_color_space())) {

			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)mc->widths.array[i], 0.0f,
				 (float)mc->heights.array[i], -100.0f, 100.0f);

			obs_view_render(view);

			gs_texrender_end(mc->renders.array[i]);
		}
		gs_blend_state_pop();

		gs_texture_t *tex =
			gs_texrender_get_texture(mc->renders.array[i]);
		if (tex) {
			const bool previous = gs_framebuffer_srgb_enabled();
			gs_enable_framebuffer_srgb(true);

			gs_effect_set_texture_srgb(
				gs_effect_get_param_by_name(effect, "image"),
				tex);

			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(tex, 0, mc->widths.array[i],
					       mc->heights.array[i]);

			gs_enable_framebuffer_srgb(previous);
		}

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
	gs_texrender_t *render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	da_push_back(mc->renders, &render);

	multi_canvas_update_size(mc);
}

void multi_canvas_source_remove_view(void *data, obs_view_t *view)
{
	struct multi_canvas_info *mc = data;
	for (size_t i = 0; i < mc->views.num; i++) {
		if (mc->views.array[i] == view) {
			gs_texrender_destroy(mc->renders.array[i]);
			da_erase(mc->views, i);
			da_erase(mc->widths, i);
			da_erase(mc->heights, i);
			da_erase(mc->renders, i);
			break;
		}
	}
	multi_canvas_update_size(mc);
}

struct obs_source_info multi_canvas_source = {
	.id = "vertical_multi_canvas_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED |
			OBS_SOURCE_CUSTOM_DRAW,
	.get_name = multi_canvas_get_name,
	.create = multi_canvas_create,
	.destroy = multi_canvas_destroy,
	.video_render = multi_canvas_video_render,
	.get_width = multi_canvas_get_width,
	.get_height = multi_canvas_get_height,
};
