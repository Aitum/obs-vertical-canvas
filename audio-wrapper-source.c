
#include <obs-module.h>
#include "audio-wrapper-source.h"

const char *audio_wrapper_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "transition_audio_wrapper";
}

void *audio_wrapper_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct audio_wrapper_info *audio_wrapper = bzalloc(sizeof(struct audio_wrapper_info));
	audio_wrapper->source = source;
	return audio_wrapper;
}

void audio_wrapper_destroy(void *data)
{
	bfree(data);
}

bool audio_wrapper_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers, size_t channels,
			  size_t sample_rate)
{
	UNUSED_PARAMETER(sample_rate);
	struct audio_wrapper_info *aw = (struct audio_wrapper_info *)data;
	obs_source_t *source = aw->target(aw->param);
	if (!source)
		return false;

	uint64_t timestamp = obs_source_get_audio_timestamp(source);
	if (!timestamp) {
		obs_source_release(source);
		return false;
	}
	if (!aw->mixers) {
		*ts_out = timestamp;
		obs_source_release(source);
		return true;
	}
	mixers &= aw->mixers(aw->param);
	if (mixers == 0) {
		*ts_out = timestamp;
		obs_source_release(source);
		return true;
	}
	struct obs_source_audio_mix child_audio;
	obs_source_get_audio_mix(source, &child_audio);
	for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
		if ((mixers & (1 << mix)) == 0)
			continue;

		for (size_t ch = 0; ch < channels; ch++) {
			float *out = audio->output[mix].data[ch];
			float *in = child_audio.output[mix].data[ch];
			float *end = in + AUDIO_OUTPUT_FRAMES;
			while (in < end)
				*(out++) += *(in++);
		}
	}
	*ts_out = timestamp;
	obs_source_release(source);
	return true;
}

static void audio_wrapper_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param, bool active)
{
	UNUSED_PARAMETER(active);
	struct audio_wrapper_info *aw = (struct audio_wrapper_info *)data;
	obs_source_t *source = aw->target(aw->param);
	if (!source)
		return;

	enum_callback(aw->source, source, param);

	obs_source_release(source);
}

void audio_wrapper_enum_active_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	audio_wrapper_enum_sources(data, enum_callback, param, true);
}

void audio_wrapper_enum_all_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	audio_wrapper_enum_sources(data, enum_callback, param, false);
}

struct obs_source_info audio_wrapper_source = {
	.id = "vertical_audio_wrapper_source",
	.type = OBS_SOURCE_TYPE_SCENE,
	.output_flags = OBS_SOURCE_COMPOSITE | OBS_SOURCE_CAP_DISABLED,
	.get_name = audio_wrapper_get_name,
	.create = audio_wrapper_create,
	.destroy = audio_wrapper_destroy,
	.audio_render = audio_wrapper_render,
	.enum_active_sources = audio_wrapper_enum_active_sources,
	.enum_all_sources = audio_wrapper_enum_all_sources,
};
