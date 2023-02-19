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

  size_t terrainVertices;
};

Context ctx;

struct TerrainMeshAttribute {
  PuleF32v3 origin;
};

void initializeContext(
  float const * const heights,
  size_t const width, size_t const height,
  bool isEditor=false
) {
  PuleError err = puleError();

  float const mapdim = 100.0f;

  std::vector<TerrainMeshAttribute> attributes;
  { // parse heightfield to mesh
    float originStartX = -mapdim/2.0f;
    float originStartY = -mapdim/2.0f;
    float originItX = mapdim/(float)width;
    float originItY = mapdim/(float)height;

    for (size_t itx = 0; itx < width-1; ++ itx)
    for (size_t ity = 0; ity < height-1; ++ ity) {
      auto const ul = PuleF32v3 {
        originStartX + itx*originItX,
        heights[ity*height + itx],
        originStartY + ity*originItY,
      };
      auto const ur = PuleF32v3 {
        originStartX + (itx+1)*originItX,
        heights[ity*height + (itx+1)],
        originStartY + ity*originItY,
      };
      auto const ll = PuleF32v3 {
        originStartX + itx*originItX,
        heights[(ity+1)*height + itx],
        originStartY + (ity+1)*originItY,
      };
      auto const lr = PuleF32v3 {
        originStartX + (itx+1)*originItX,
        heights[(ity+1)*height + (itx+1)],
        originStartY + (ity+1)*originItY,
      };
      attributes.emplace_back(ul);
      attributes.emplace_back(ur);
      attributes.emplace_back(lr);
      attributes.emplace_back(lr);
      attributes.emplace_back(ll);
      attributes.emplace_back(ul);
    }
  }
  ctx.terrainVertices = attributes.size();

  ctx.bufferAttributesStatic = (
    pul.gfxGpuBufferCreate(
      attributes.data(),
      sizeof(TerrainMeshAttribute) * attributes.size(),
      PuleGfxGpuBufferUsage_bufferAttribute,
      (
        isEditor
        ? PuleGfxGpuBufferVisibilityFlag_hostWritable
        : PuleGfxGpuBufferVisibilityFlag_deviceOnly
      )
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
          int triangleID = gl_VertexID/3;
          outUv = (
            vec3(
              triangleID%20/20.0f,
              triangleID%5/5.0f,
              mod((triangleID+1), 3.3)/3.3f
            )
          );
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
}

void terrainRender(
  PuleGfxFramebuffer const framebuffer,
  PuleGfxCommandListRecorder const recorder
) {
  pul.gfxCommandListAppendAction(
    recorder,
    PuleGfxCommand {
      .bindPipeline = {
        .action = PuleGfxAction_bindPipeline,
        .pipeline = ctx.pipeline,
      },
    }
  );
  pul.gfxCommandListAppendAction(
    recorder,
    PuleGfxCommand {
      .bindFramebuffer = {
        .action = PuleGfxAction_bindFramebuffer,
        .framebuffer = framebuffer,
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
      puleProjectionPerspective(90.0f, 800.0f/600.0f, 0.001f, 1000.0f)
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
        .numVertices = ctx.terrainVertices,
      },
    }
  );
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

  // TODO load
  /* std::vector<float> heights = { 1.0f, 2.0f, 1.0f, 1.0f }; */
    std::vector<float> defaultTerrainValues;
    defaultTerrainValues.resize(100*100);
    for (size_t itx = 0; itx < 100; ++ itx)
    for (size_t ity = 0; ity < 100; ++ ity) {
      defaultTerrainValues.emplace_back(1.0f + (itx%20)*5.0f + (ity%50)*6.5f);
    }
  initializeContext(defaultTerrainValues.data(), 100, 100, false);
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

  ::terrainRender(PuleGfxFramebuffer{0}, recorder);
}

} // extern C

// -- editor -------------------------------------------------------------------
#include <pulchritude-imgui/imgui.h>

namespace {

PuleGfxFramebuffer guiFramebuffer;
PuleGfxGpuImage guiImageColor;
PuleGfxGpuImage guiImageDepth;
PuleGfxCommandList guiCommandList;
PuleGfxCommandListRecorder guiCommandListRecorder;
std::vector<float> guiHeightmap;
void * guiMappedAttributes;

void guiInitialize() {
  static bool initialized = false;
  if (initialized) { return; }
  initialized = true;

  PuleDsValue const dsTerrain = puleDsCreateObject(puleAllocateDefault());
  { // store default terrain
    std::vector<float> defaultTerrainValues;
    defaultTerrainValues.resize(100*100);
    for (size_t itx = 0; itx < 100; ++ itx)
    for (size_t ity = 0; ity < 100; ++ ity) {
      defaultTerrainValues.emplace_back(1.0f + (itx%20)*5.0f + (ity%50)*6.5f);
    }
    guiHeightmap = defaultTerrainValues;
    (void)dsTerrain;
    /* puleDsObjectMemberAssign( */
    /*   dsTerrain, */
    /*   puleCStr("heightmap-buffer"), */
    /*   puleDsCreateBuffer( */
    /*     puelAllocateDefault(), */
    /*     PuleArrayView { */
    /*       .data = defaultTerrainValues.data(), */
    /*       .elementStride = sizeof(float), */
    /*       .elementCount = defaultTerrainValues.size() */
    /*     } */
    /*   ) */
    /* ); */
    /* puleDsObjectMemberAssign( */
    /*   dsTerrain, puleCStr("heightmap-width"), puleDsCreateI64(100) */
    /* ); */
    /* puleDsObjectMemberAssign( */
    /*   dsTerrain, puleCStr("heightmap-height"), puleDsCreateI64(100) */
    /* ); */
  }

  // load terrain into context
  initializeContext(guiHeightmap.data(), 100, 100, true);

  // gui mapped pointers
  guiMappedAttributes = (
    pul.gfxGpuBufferMap({
      .buffer = ctx.bufferAttributesStatic,
      .access = PuleGfxGpuBufferMapAccess_hostWritable,
      .byteOffset = 0,
      .byteLength = sizeof(TerrainMeshAttribute) * 100*100,
    })
  );

  // gui command list

  guiCommandList = (
    puleGfxCommandListCreate(puleAllocateDefault(), puleCStr("terrain-gui"))
  );
  guiCommandListRecorder = (
    puleGfxCommandListRecorder(guiCommandList)
  );

  { // gui image / framebuffer
    PuleGfxSampler sampler = (
      puleGfxSamplerCreate({
        .minify = PuleGfxImageMagnification_nearest,
        .magnify = PuleGfxImageMagnification_nearest,
        .wrapU = PuleGfxImageWrap_clampToEdge,
        .wrapV = PuleGfxImageWrap_clampToEdge,
      })
    );
    guiImageColor = (
      puleGfxGpuImageCreate({
        .width = 800,
        .height = 600,
        .target = PuleGfxImageTarget_i2D,
        .byteFormat = PuleGfxImageByteFormat_rgba8U,
        .sampler = sampler,
        .optionalInitialData = nullptr,
      })
    );
    guiImageDepth = (
      puleGfxGpuImageCreate({
        .width = 800,
        .height = 600,
        .target = PuleGfxImageTarget_i2D,
        .byteFormat = PuleGfxImageByteFormat_depth16,
        .sampler = sampler,
        .optionalInitialData = nullptr,
      })
    );
    PuleGfxFramebufferCreateInfo fbci = puleGfxFramebufferCreateInfo();
    fbci.attachmentType = PuleGfxFramebufferType_imageStorage;
    fbci.attachment.images[PuleGfxFramebufferAttachment_color0] = {
      .image = guiImageColor, .mipmapLevel = 0,
    };
    fbci.attachment.images[PuleGfxFramebufferAttachment_depth] = {
      .image = guiImageDepth, .mipmapLevel = 0,
    };
    PuleError err = puleError();
    guiFramebuffer = puleGfxFramebufferCreate(fbci, &err);
    if (puleErrorConsume(&err)) { return; }
  }
}

} // namespace

extern "C" {
void puldGuiEditor(
  [[maybe_unused]] PuleAllocator const allocator,
  PulePlatform const platform,
  PuleEngineLayer const pulLayer
) {
  ::pul = pulLayer;
  guiInitialize();
  static bool open = true;
  pul.imguiWindowBegin("terrain", &open);
  if (!open) {
    return;
  }

  pul.gfxCommandListRecorderReset(guiCommandListRecorder);
  pul.gfxCommandListAppendAction(
    guiCommandListRecorder,
    PuleGfxCommand {
      .clearFramebufferColor = {
        .action = PuleGfxAction_clearFramebufferColor,
        .framebuffer = guiFramebuffer,
        .color = PuleF32v4(0.2f, 0.3f, 0.2f, 1.0f),
      },
    }
  );
  pul.gfxCommandListAppendAction(
    guiCommandListRecorder,
    PuleGfxCommand {
      .clearFramebufferDepth = {
        .action = PuleGfxAction_clearFramebufferDepth,
        .framebuffer = guiFramebuffer,
        .depth = 1.0f,
      },
    }
  );
  ::terrainRender(guiFramebuffer, guiCommandListRecorder);
  pul.gfxCommandListRecorderFinish(guiCommandListRecorder);
  PuleError err = pul.error();
  pul.gfxCommandListSubmit(
    PuleGfxCommandListSubmitInfo {
      .commandList = guiCommandList,
      .fenceTargetStart = nullptr,
      .fenceTargetFinish = nullptr,
    },
    &err
  );
  if (pul.errorConsume(&err)) { return; }

  PuleI32v2 mouseOrigin = (
    pul.i32v2Sub(pulePlatformMouseOrigin(platform), puleImguiCurrentOrigin())
  );
  pul.imguiImage(
    guiImageColor, PuleF32v2{400, 400}, pul.f32v2(0), pul.f32v2(1),
    pul.f32v4(1)
  );
  puleImguiText("Mouse origin <%u, %u> %s)", mouseOrigin.x, mouseOrigin.y,
    pul.imguiLastItemHovered() ? "hovered" : "");
  if (pul.imguiLastItemHovered()) {
  }

  pul.imguiWindowEnd();
}

} // extern C
