#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct audio_wrapper_info {
	obs_source_t *source;
	void *param;
	obs_source_t *(*target)(void *param);
	uint32_t (*mixers)(void *param);
};

extern struct obs_source_info audio_wrapper_source;

#ifdef __cplusplus
};
#endif
