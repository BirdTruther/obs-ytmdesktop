#include <obs-module.h>
#include <plugin-support.h>

#include "ytmd-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	register_ytmd_source();
	obs_log(LOG_INFO, "obs-ytmdesktop loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "obs-ytmdesktop unloaded");
}
