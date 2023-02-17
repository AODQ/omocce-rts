#include <pulchritude-log/log.h>
#include <pulchritude-plugin/plugin.h>
#include <pulchritude-plugin/engine.h>
#include <pulchritude-gfx/gfx.h>

#include <vector>

namespace {
PuleEngineLayer pul;
} // namespace

// -- render -------------------------------------------------------------------
namespace {

struct Context {
  PuleGfxCommandList commandList;
  PuleGfxShaderModule shaderModule;
  PuleGfxGpuBuffer bufferAttributesStatic;
  PuleGfxPipeline pipeline;
};

Context ctx;

struct TerrainMeshAttribute {
  PuleF32v3 origin;
};

void initializeContext() {
  PuleError err = puleError();

  std::vector<TerrainMeshAttribute> attributes = {
    { .origin = { PuleF32v3 { -3.0f, 0.0f, -3.0f, }, }, },
    { .origin = { PuleF32v3 { +3.0f, 0.0f, -3.0f, }, }, },
    { .origin = { PuleF32v3 { +3.0f, 0.0f, +3.0f, }, }, },
    { .origin = { PuleF32v3 { +3.0f, 0.0f, +3.0f, }, }, },
    { .origin = { PuleF32v3 { -3.0f, 0.0f, +3.0f, }, }, },
    { .origin = { PuleF32v3 { -3.0f, 0.0f, -3.0f, }, }, },
  };

  ctx.bufferAttributesStatic = (
    pul.gfxGpuBufferCreate(
      attributes.data(),
      sizeof(TerrainMeshAttribute) * attributes.size(),
      PuleGfxGpuBufferUsage_bufferAttribute,
      PuleGfxGpuBufferVisibilityFlag_deviceOnly
    )
  );

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
          vec3 origin = inOrigin;
          gl_Position = (projection * view) * vec4(origin, 1.0f);
          outUv = vec3(gl_Position.xyz);
        }
      ),
      // FRAGMENT
      SHADER(
        in layout(location = 0) vec3 inUv;
        out layout(location = 0) vec4 outColor;

        void main() {
          outColor = vec4(inUv, 1.0f);
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
      .stridePerElement = sizeof(TerrainMeshAttribute),
      .offsetIntoBuffer = offsetof(TerrainMeshAttribute, origin),
    };

    auto pipelineInfo = PuleGfxPipelineCreateInfo {
      .shaderModule = ctx.shaderModule,
      .framebuffer = pul.gfxFramebufferWindow(),
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

  /* // create command list */
  /* ctx.commandList = ( */
    /* pul.gfxCommandListCreate( */
      /* pul.allocateDefault(), pul.cStr("node-unit-render") */
    /* ) */
  /* ); */
  /* { */
  /* } */
}

} // namespace

extern "C" {

PulePluginType pulcPluginType() {
  return PulePluginType_component;
}

void pulcComponentLoad(PulePluginPayload const payload) {
  ::pul = *reinterpret_cast<PuleEngineLayer *>(
    pulePluginPayloadFetch(payload, puleCStr("pule-engine-layer"))
  );

  initializeContext();
}

void pulcComponentUpdate(PulePluginPayload const payload) {
  auto const taskGraph = PuleTaskGraph {
    .id = pul.pluginPayloadFetchU64(
      payload,
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


  pul.gfxCommandListAppendAction(
    recorder,
    PuleGfxCommand {
      .bindPipeline = {
        .action = PuleGfxAction_bindPipeline,
        .pipeline = ctx.pipeline,
      },
    }
  );
  static float time = 0.0f;
  time += 1.0f/60.0f;
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

  pul.gfxCommandListAppendAction(
    recorder,
    PuleGfxCommand {
      .dispatchRender = {
        .action = PuleGfxAction_dispatchRender,
        .drawPrimitive = PuleGfxDrawPrimitive_triangle,
        .vertexOffset = 0,
        .numVertices = 6,
      },
    }
  );
}

} // extern C
