
#include "multi-canvas.hpp"
#include "version.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Aitum");
OBS_MODULE_USE_DEFAULT_LOCALE("transition-table", "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Multi Canvas] loaded version %s", PROJECT_VERSION);
	return true;
}

void obs_module_unload(void)
{

}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("MultiCanvas");
}


