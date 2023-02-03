#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct audio_wrapper_info {
	obs_source_t *source;
	obs_weak_source_t *target;
};

extern struct obs_source_info audio_wrapper_source;

#ifdef __cplusplus
};
#endif