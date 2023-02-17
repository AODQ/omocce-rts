#include <pulchritude-log/log.h>
#include <pulchritude-plugin/plugin.h>
#include <pulchritude-plugin/engine.h>

#include "components/module.h"
#include "components/node-unit.h"

#include "graph.h"

namespace {
PuleEngineLayer pul;
PuleEcsWorld world;
PulePlatform platform;
PulePluginPayload payload;
}

extern "C" {

PulePluginPayload pulcPluginPayload() {
  return payload;
}

PuleEngineLayer * pulcEngineLayer() {
  return &::pul;
}

PulePluginType pulcPluginType() {
  return PulePluginType_component;
}

void pulcComponentLoad(PulePluginPayload const newPayload) {
  ::payload = newPayload;
  ::pul = *reinterpret_cast<PuleEngineLayer *>(
    pulePluginPayloadFetch(::payload, puleCStr("pule-engine-layer"))
  );

  pul.log("graph plugin loaded");

  ::world = PuleEcsWorld {
    pulePluginPayloadFetchU64(::payload, puleCStr("pule-ecs-world"))
  };
  ::platform = PulePlatform {
    pulePluginPayloadFetchU64(::payload, puleCStr("pule-platform"))
  };

  PuleEcsEntity const testEntity = pul.ecsEntityCreate(world, pul.cStr("tete"));
  pul.pluginPayloadStoreU64(::payload, pul.cStr("test-entity"), testEntity.id);

  PulcComponentNodeUnit unit{ .position = pul.f32v2(1.0f), };
  pul.ecsEntityAttachComponent(
    world,
    testEntity,
    pul.ecsComponentFetchByLabel(world, pul.cStr("PulcComponentNodeUnit")),
    &unit
  );

  systemNodeUnitRenderInitialize();
}

void pulcComponentUpdate(PulePluginPayload const) {
  PuleEcsEntity const testEntity = {
    .id = pul.pluginPayloadFetchU64(payload, pul.cStr("test-entity")),
  };
  assert(testEntity.id);
  auto & nodeUnit = *reinterpret_cast<PulcComponentNodeUnit const *>(
    pul.ecsEntityComponentData(
      world, testEntity,
      pul.ecsComponentFetchByLabel(world, pul.cStr("PulcComponentNodeUnit"))
    )
  );
  assert(nodeUnit.position.x == 1.0f);
}

void pulcComponentUnload(PulePluginPayload const) {
  pul.pluginPayloadRemove(payload, pul.cStr("test-entity"));
}

} // extern C
