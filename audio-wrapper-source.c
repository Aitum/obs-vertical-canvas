
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
	struct audio_wrapper_info *audio_wrapper =
		bzalloc(sizeof(struct audio_wrapper_info));
	audio_wrapper->source = source;
	return audio_wrapper;
}

void audio_wrapper_destroy(void *data)
{
	bfree(data);
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - t;
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t;
}

bool audio_wrapper_render(void *data, uint64_t *ts_out,
			     struct obs_source_audio_mix *audio,
			     uint32_t mixers, size_t channels,
			     size_t sample_rate){
	struct audio_wrapper_info *aw = (struct audio_wrapper_info *)data;
	obs_source_t *source = obs_weak_source_get_source(aw->target);
	if(!source)
		return false;

	bool result = obs_transition_audio_render(source, ts_out, audio, mixers,
					   channels, sample_rate, mix_a, mix_b);
	obs_source_release(source);
	return result;
}

struct obs_source_info audio_wrapper_source = {
	.id = "transition_audio_wrapper_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_CAP_DISABLED,
	.get_name = audio_wrapper_get_name,
	.create = audio_wrapper_create,
	.destroy = audio_wrapper_destroy,
	.audio_render = audio_wrapper_render,
};
