#include "module.h"

#include <pulchritude-gfx/commands.h>
#include <pulchritude-log/log.h>
#include <pulchritude-math/math.h>

#include "../components/node-unit.h"
#include "../graph.h"

#include <vector>

namespace { // -----------------------------------------------------------------

struct EntityAttributeStatic {
  PuleF32v3 origin;
  /* PuleF32v2 uv; */
  /* PuleF32v4 normal; */
};

struct EntityAttributeDynamic {
  PuleF32m44 transform;
};

struct Context {
  PuleGfxCommandList commandList;
  PuleGfxShaderModule shaderModule;
  PuleGfxGpuBuffer bufferAttributesDynamic;
  PuleGfxGpuBuffer bufferAttributesStatic;
  PuleGfxGpuBuffer bufferIndirect;
  PuleGfxPipeline pipeline;

  EntityAttributeDynamic * mappedAttributesDynamic;
  PuleGfxDrawIndirectArrays * mappedDrawIndirect;

  size_t entityCount;
  size_t entityCapacity;
};

Context ctx;

} // namespace -----------------------------------------------------------------

void systemNodeUnitRenderInitialize() {
  PuleEngineLayer & pul = *pulcEngineLayer();
  PuleError err = pul.error();

  ctx.entityCount = 0;
  ctx.entityCapacity = 128;

  std::vector<EntityAttributeStatic> meshAttributes;

    float const a = 1.0f / 3.0f;
    float const b = sqrtf(8.0f / 9.0f);
    float const c = sqrtf(2.0f / 9.0f);
    float const d = sqrtf(2.0f / 3.0f);

    auto v0 = PuleF32v3{0, 0, 1};
    auto v1 = PuleF32v3{-c, d, -a};
    auto v2 = PuleF32v3{-c, -d, -a};
    auto v3 = PuleF32v3{b, 0, -a};
  for (size_t it = 0; it < ctx.entityCapacity; ++ it) {
    for (auto orig : std::vector<PuleF32v3> {
      v0, v1, v2,
      v0, v2, v3,
      v0, v3, v1,
      v3, v2, v1,
    }) {
      meshAttributes.emplace_back(
        EntityAttributeStatic {
          .origin = orig,
          /* .uv = PuleF32v2 { 1.0f, 1.0f, }, */
          /* .normal = PuleF32v4 { 1.0f, 1.0f, 1.0f, 1.0f, }, */
        }
      );
    }
  }

  { // create buffers
    ctx.bufferAttributesStatic = (
      pul.gfxGpuBufferCreate(
        meshAttributes.data(),
        sizeof(EntityAttributeStatic) * meshAttributes.size(),
        PuleGfxGpuBufferUsage_bufferAttribute,
        PuleGfxGpuBufferVisibilityFlag_deviceOnly
      )
    );
    ctx.bufferAttributesDynamic = (
      pul.gfxGpuBufferCreate(
        nullptr,
        sizeof(EntityAttributeDynamic) * ctx.entityCapacity,
        PuleGfxGpuBufferUsage_bufferAttribute,
        PuleGfxGpuBufferVisibilityFlag_hostWritable
      )
    );
    ctx.bufferIndirect = (
      pul.gfxGpuBufferCreate(
        nullptr,
        sizeof(PuleGfxDrawIndirectArrays),
        PuleGfxGpuBufferUsage_bufferIndirect,
        PuleGfxGpuBufferVisibilityFlag_hostWritable
      )
    );
  }

  ctx.mappedAttributesDynamic = (
    reinterpret_cast<EntityAttributeDynamic *>(
      pul.gfxGpuBufferMap({
        .buffer = ctx.bufferAttributesDynamic,
        .access = PuleGfxGpuBufferMapAccess_hostWritable,
        .byteOffset = 0,
        .byteLength = sizeof(EntityAttributeDynamic) * ctx.entityCapacity,
      })
    )
  );

  ctx.mappedDrawIndirect = (
    reinterpret_cast<PuleGfxDrawIndirectArrays *>(
      pul.gfxGpuBufferMap({
        .buffer = ctx.bufferIndirect,
        .access = PuleGfxGpuBufferMapAccess_hostWritable,
        .byteOffset = 0,
        .byteLength = sizeof(PuleGfxDrawIndirectArrays),
      })
    )
  );

  PULE_assert(ctx.mappedAttributesDynamic);
  PULE_assert(ctx.mappedDrawIndirect);

  #define SHADER(...) \
    pul.cStr( \
    "#version 460 core\n" \
    #__VA_ARGS__ \
    )

  ctx.shaderModule = (
    pul.gfxShaderModuleCreate(
      // VERTEX
      SHADER(
        in layout(location = 0) vec3 inOrigin;
        /* in layout(location = 1) vec2 inUv; */
        /* in layout(location = 2) vec4 inNormal; */

        uniform layout(location = 0) mat4 view;
        uniform layout(location = 1) mat4 projection;

        out layout(location = 0) vec3 outUv;

        void main() {
          vec3 origin = inOrigin + vec3(gl_InstanceID*2.0f, 0.0f, 1.0f);
          gl_Position = (projection * view) * vec4(origin, 1.0f);
          outUv = vec3(gl_VertexID/3 + 24);
        }
      ),
      // FRAGMENT
      SHADER(
        in layout(location = 0) vec3 inUv;
        out layout(location = 0) vec4 outColor;

        void main() {
          outColor = vec4(mod(inUv.x, 2.0f), mod(inUv.x, 4.5f), mod(inUv.x, 8.0f), 1.0f);
        }
      ),
      &err
    )
  );
  if (pul.errorConsume(&err)) { return; }

  { // create pipeline
    auto descriptorSetLayout = pul.gfxPipelineDescriptorSetLayout();
    descriptorSetLayout.bufferAttributeBindings[0] = {
      .buffer = ctx.bufferAttributesStatic,
      .numComponents = 3,
      .dataType = PuleGfxAttributeDataType_float,
      .convertFixedDataTypeToNormalizedFloating = false,
      .stridePerElement = sizeof(EntityAttributeStatic),
      .offsetIntoBuffer = offsetof(EntityAttributeStatic, origin),
    };
    /* descriptorSetLayout.bufferAttributeBindings[1] = { */
    /*   .buffer = ctx.bufferAttributesStatic, */
    /*   .numComponents = 2, */
    /*   .dataType = PuleGfxAttributeDataType_float, */
    /*   .convertFixedDataTypeToNormalizedFloating = false, */
    /*   .stridePerElement = sizeof(EntityAttributeStatic), */
    /*   .offsetIntoBuffer = offsetof(EntityAttributeStatic, uv), */
    /* }; */
    /* descriptorSetLayout.bufferAttributeBindings[2] = { */
    /*   .buffer = ctx.bufferAttributesStatic, */
    /*   .numComponents = 4, */
    /*   .dataType = PuleGfxAttributeDataType_float, */
    /*   .convertFixedDataTypeToNormalizedFloating = false, */
    /*   .stridePerElement = sizeof(EntityAttributeStatic), */
    /*   .offsetIntoBuffer = offsetof(EntityAttributeStatic, normal), */
    /* }; */

    auto pipelineInfo = PuleGfxPipelineCreateInfo {
      .shaderModule = ctx.shaderModule,
      .layout = &descriptorSetLayout,
      .config = {
        .depthTestEnabled = true,
        .blendEnabled = false,
        .scissorTestEnabled = false,
        .viewportUl = PuleI32v2 { 0, 0, },
        .viewportLr = PuleI32v2 { 800, 600, },
        .scissorUl = PuleI32v2 { 0, 0, },
        .scissorLr = PuleI32v2 { 800, 600, },
      },
    };

    ctx.pipeline = pul.gfxPipelineCreate(&pipelineInfo, &err);
    if (pul.errorConsume(&err) > 0) {
      return;
    }
  }

  // create command list
  ctx.commandList = (
    pul.gfxCommandListCreate(
      pul.allocateDefault(), pul.cStr("node-unit-render")
    )
  );
  {
    auto commandListRecorder = pul.gfxCommandListRecorder(ctx.commandList);

    pul.gfxCommandListAppendAction(
      commandListRecorder,
      PuleGfxCommand {
        .dispatchRenderIndirect = {
          .action = PuleGfxAction_dispatchRenderIndirect,
          .drawPrimitive = PuleGfxDrawPrimitive_triangle,
          .bufferIndirect = ctx.bufferIndirect,
          .byteOffset = 0,
        },
      }
    );

    pul.gfxCommandListRecorderFinish(commandListRecorder);
  }
}

extern "C" {

void pulcSystemCallbackNodeUnitRender(
  PuleEcsIterator const iter
) {
  PuleEngineLayer & pul = *pulcEngineLayer();

  PulcComponentNodeUnit * nodeUnits = (
    reinterpret_cast<PulcComponentNodeUnit *>(
      pul.ecsIteratorQueryComponents(iter, 0, sizeof(PulcComponentNodeUnit))
    )
  );

  PuleGfxDrawIndirectArrays indirectCommand = {
    .vertexCount = 12, .instanceCount = 2,
    .vertexOffset = 0, .instanceOffset = 0,
  };

  size_t const entityCount = pul.ecsIteratorEntityCount(iter);
  for (size_t it = 0; it < entityCount; ++ it) {
    [[maybe_unused]] PulcComponentNodeUnit & unit = nodeUnits[it];
    indirectCommand.instanceCount += 2;
    ctx.mappedAttributesDynamic[it].transform = pul.f32m44(1.0f);
  }

  memcpy(
    ctx.mappedDrawIndirect,
    &indirectCommand,
    sizeof(PuleGfxDrawIndirectArrays)
  );
  pul.gfxGpuBufferMappedFlush({
    .buffer = ctx.bufferIndirect,
    .byteOffset = 0, .byteLength = sizeof(PuleGfxDrawIndirectArrays)
  });
  pul.gfxGpuBufferMappedFlush({
    .buffer = ctx.bufferAttributesDynamic,
    .byteOffset = 0,
    .byteLength = sizeof(EntityAttributeDynamic) * ctx.entityCount,
  });

  auto const taskGraph = PuleTaskGraph {
    .id = pul.pluginPayloadFetchU64(
      pulcPluginPayload(),
      pul.cStr("pule-render-task-graph")
    ),
  };
  PuleTaskGraphNode const renderGeometryNode = (
    pul.taskGraphNodeFetch(taskGraph, pul.cStr("render-geometry"))
  );

  auto const recorder = PuleGfxCommandListRecorder {
    pul.taskGraphNodeAttributeFetchU64(
      renderGeometryNode,
      pul.cStr("command-list-primary-recorder")
    )
  };

  static float time = 0.0f;
  time += 1.0f/60.0f;

  pul.gfxCommandListAppendAction(
    recorder,
    PuleGfxCommand {
      .bindPipeline = {
        .action = PuleGfxAction_bindPipeline,
        .pipeline = ctx.pipeline,
      },
    }
  );
  { // push constant
    PuleF32m44 const view = (
      puleViewLookAt(
        PuleF32v3{sinf(time)*3.0f, 1.0f + 0.5f*cosf(time*0.5f), cosf(time)*3.0f},
        puleF32v3(0.0),
        PuleF32v3{0.0f, 1.0f, 0.0f}
      )
    );
    PuleF32m44 const proj = (
      puleProjectionPerspective(90.0f, 1.0f, 0.001f, 1000.0f)
    );
    std::vector<PuleGfxConstant> pushConstants = {
      {
        .value = { .constantF32m44 = view, },
        .typeTag = PuleGfxConstantTypeTag_f32m44,
        .bindingSlot = 0,
      },
      {
        .value = { .constantF32m44 = proj, },
        .typeTag = PuleGfxConstantTypeTag_f32m44,
        .bindingSlot = 1,
      },
    };
    pul.gfxCommandListAppendAction(
      recorder,
      PuleGfxCommand {
        .pushConstants = {
          .action = PuleGfxAction_pushConstants,
          .constants = pushConstants.data(),
          .constantsLength = pushConstants.size(),
        },
      }
    );
  }
  /* pul.gfxCommandListAppendAction( */
  /*   recorder, */
  /*   PuleGfxCommand { */
  /*     .dispatchCommandList = { */
  /*       .action = PuleGfxAction_dispatchCommandList, */
  /*       .submitInfo = PuleGfxCommandListSubmitInfo { */
  /*         .commandList = ctx.commandList, */
  /*         .fenceTargetStart = nullptr, // TODO maybe needs fence? MT tho...? */
  /*         .fenceTargetFinish = nullptr, */
  /*       }, */
  /*     }, */
  /*   } */
  /* ); */
}

} // C
