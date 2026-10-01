#include "vinox/plugins.h"

extern "C" VINOX_API vinox_status VINOX_CALL vinox_plugin_get_api_v1(vinox_tool_plugin* plugin_out) {
    if (!plugin_out) return VINOX_STATUS_INVALID_ARGUMENT;
    vinox_tool_plugin* plug = nullptr;
    vinox_status st = vinox_plugin_std_math_create(&plug);
    if (st != VINOX_STATUS_OK || !plug) return st;
    *plugin_out = *plug;
    delete plug;
    return VINOX_STATUS_OK;
}
