#pragma once

#include <pulchritude-plugin/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

PulePluginPayload pulcPluginPayload();
PuleEngineLayer * pulcEngineLayer();

#ifdef __cplusplus
} // C
#endif

void systemNodeUnitRenderInitialize();
