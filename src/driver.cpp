#include "moana/driver.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>

#include <cuda_runtime.h>
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include "assert_macros.hpp"
#include "core/ptex_texture.hpp"
#include "util/enumerate.hpp"
#include "kernel.hpp"
#include "moana/core/vec3.hpp"
#include "moana/io/image.hpp"
#include "moana/parsers/obj_parser.hpp"
#include "scene/container.hpp"
#include "scene/materials.hpp"
#include "scene/texture_offsets.hpp"
#include "util/color_map.hpp"

namespace moana {

template <typename T>
struct SbtRecord
{
    __align__(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

typedef SbtRecord<RayGenData> RayGenSbtRecord;
typedef SbtRecord<MissData> MissSbtRecord;
typedef SbtRecord<HitGroupData> HitGroupSbtRecord;

static void contextLogCallback(
    unsigned int level,
    const char *tag,
    const char *message,
    void * /*cbdata */
)
{
    std::cerr << "[" << std::setw(2) << level << "][" << std::setw(12) << tag << "]: "
              << message << std::endl;
}

static void createContext(OptixState &state)
{
    // initialize CUDA
    CHECK_CUDA(cudaFree(0));

    CHECK_OPTIX(optixInit());

    OptixDeviceContextOptions options = {};
    options.logCallbackFunction = &contextLogCallback;
    options.logCallbackLevel = 4;

    CUcontext cuContext = 0; // current context
    CHECK_OPTIX(optixDeviceContextCreate(cuContext, &options, &state.context));
}

static void createModule(OptixState &state)
{
    state.moduleCompileOptions = {};
    state.moduleCompileOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    state.moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    state.moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_LINEINFO;

    state.pipelineCompileOptions.usesMotionBlur = false;
    state.pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
    state.pipelineCompileOptions.numPayloadValues = 3;
    state.pipelineCompileOptions.numAttributeValues = 3;
#ifdef DEBUG
    state.pipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_DEBUG | OPTIX_EXCEPTION_FLAG_TRACE_DEPTH | OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW;
#else
    state.pipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
#endif
    state.pipelineCompileOptions.pipelineLaunchParamsVariableName = "params";
    state.pipelineCompileOptions.usesPrimitiveTypeFlags =
        OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE |
        OPTIX_PRIMITIVE_TYPE_FLAGS_ROUND_CUBIC_BSPLINE;

    std::string ptx(ptxSource);

    char log[2048];
    size_t sizeofLog = sizeof(log);

    CHECK_OPTIX(optixModuleCreateFromPTX(
        state.context,
        &state.moduleCompileOptions,
        &state.pipelineCompileOptions,
        ptx.c_str(),
        ptx.size(),
        log,
        &sizeofLog,
        &state.module
    ));
}

static void createProgramGroups(OptixState &state)
{
    OptixProgramGroupOptions programGroupOptions = {};

    OptixProgramGroupDesc raygenProgramGroupDesc = {};
    raygenProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygenProgramGroupDesc.raygen.module = state.module;
    raygenProgramGroupDesc.raygen.entryFunctionName = "__raygen__rg";

    char log[2048];
    size_t sizeofLog = sizeof(log);

    CHECK_OPTIX(optixProgramGroupCreate(
        state.context,
        &raygenProgramGroupDesc,
        1, // program group count
        &programGroupOptions,
        log,
        &sizeofLog,
        &state.raygenProgramGroup
    ));

    OptixProgramGroupDesc missProgramGroupDesc = {};
    missProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    missProgramGroupDesc.miss.module = state.module;
    missProgramGroupDesc.miss.entryFunctionName = "__miss__ms";

    CHECK_OPTIX(optixProgramGroupCreate(
        state.context,
        &missProgramGroupDesc,
        1, // program group count
        &programGroupOptions,
        log,
        &sizeofLog,
        &state.missProgramGroup
    ));

    OptixBuiltinISOptions builtinISOptions = {};
    OptixModule geometryModule = nullptr;

    builtinISOptions.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_ROUND_CUBIC_BSPLINE;
    CHECK_OPTIX(optixBuiltinISModuleGet(
        state.context,
        &state.moduleCompileOptions,
        &state.pipelineCompileOptions,
        &builtinISOptions,
        &geometryModule
    ));

    OptixProgramGroupDesc hitgroupProgramGroupDesc = {};
    hitgroupProgramGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroupProgramGroupDesc.hitgroup.moduleCH = state.module;
    hitgroupProgramGroupDesc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
    hitgroupProgramGroupDesc.hitgroup.moduleIS = geometryModule;
    hitgroupProgramGroupDesc.hitgroup.entryFunctionNameIS = 0;

    CHECK_OPTIX(optixProgramGroupCreate(
        state.context,
        &hitgroupProgramGroupDesc,
        1, // program group count
        &programGroupOptions,
        log,
        &sizeofLog,
        &state.hitgroupProgramGroup
    ));
}

static void linkPipeline(OptixState &state)
{
    const uint32_t maxTraceDepth = 1;
    OptixProgramGroup programGroups[] = {
        state.raygenProgramGroup,
        state.missProgramGroup,
        state.hitgroupProgramGroup
    };

    OptixPipelineLinkOptions pipelineLinkOptions = {};
    pipelineLinkOptions.maxTraceDepth = maxTraceDepth;
    pipelineLinkOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_FULL;

    char log[2048];
    size_t sizeofLog = sizeof(log);

    CHECK_OPTIX(optixPipelineCreate(
        state.context,
        &state.pipelineCompileOptions,
        &pipelineLinkOptions,
        programGroups,
        sizeof(programGroups) / sizeof(programGroups[0]),
        log,
        &sizeofLog,
        &state.pipeline
    ));

    OptixStackSizes stackSizes = {};
    for(const auto &progGroup : programGroups) {
        CHECK_OPTIX(optixUtilAccumulateStackSizes(progGroup, &stackSizes));
    }

    uint32_t directCallableStackSizeFromTraversal;
    uint32_t directCallableStackSizeFromState;
    uint32_t continuationStackSize;
    CHECK_OPTIX(optixUtilComputeStackSizes(
        &stackSizes,
        maxTraceDepth,
        0, // maxCCDepth
        0, // maxDCDEpth
        &directCallableStackSizeFromTraversal,
        &directCallableStackSizeFromState,
        &continuationStackSize
    ));
    CHECK_OPTIX(optixPipelineSetStackSize(
        state.pipeline,
        directCallableStackSizeFromTraversal,
        directCallableStackSizeFromState,
        continuationStackSize,
        // 1 = obj for small details
        // 2 = instanced details yields element
        // 3 = instanced element yields scene object
        3 // maxTraversableDepth
    ));
}

static void createShaderBindingTable(OptixState &state)
{
    CUdeviceptr raygenRecord;
    const size_t raygenRecordSize = sizeof(RayGenSbtRecord);
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&raygenRecord), raygenRecordSize));

    RayGenSbtRecord raygenSbt;
    CHECK_OPTIX(optixSbtRecordPackHeader(state.raygenProgramGroup, &raygenSbt));
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(raygenRecord),
        &raygenSbt,
        raygenRecordSize,
        cudaMemcpyHostToDevice
    ));

    CUdeviceptr missRecord;
    size_t missRecordSize = sizeof(MissSbtRecord);
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&missRecord), missRecordSize));

    MissSbtRecord missSbt;
    CHECK_OPTIX(optixSbtRecordPackHeader(state.missProgramGroup, &missSbt));
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(missRecord),
        &missSbt,
        missRecordSize,
        cudaMemcpyHostToDevice
    ));

    CUdeviceptr d_hitgroupRecords;

    std::vector<HitGroupSbtRecord> hitgroupRecords;
    for (auto [i, baseColor] : enumerate(Materials::baseColors)) { // todo: better interface
        HitGroupSbtRecord hitgroupSbt;
        CHECK_OPTIX(optixSbtRecordPackHeader(state.hitgroupProgramGroup, &hitgroupSbt));
        hitgroupSbt.data.baseColor = baseColor;
        hitgroupSbt.data.useTexture = Textures::offsets[i].size() > 0;
        hitgroupSbt.data.materialID = i;
        hitgroupRecords.push_back(hitgroupSbt);
    }

    size_t hitgroupRecordSize = sizeof(HitGroupSbtRecord) * hitgroupRecords.size();

    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&d_hitgroupRecords),
        hitgroupRecordSize
    ));

    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(d_hitgroupRecords),
        hitgroupRecords.data(),
        hitgroupRecordSize,
        cudaMemcpyHostToDevice
    ));

    state.sbt.raygenRecord = raygenRecord;
    state.sbt.missRecordBase = missRecord;
    state.sbt.missRecordStrideInBytes = sizeof(MissSbtRecord);
    state.sbt.missRecordCount = 1;
    state.sbt.hitgroupRecordBase = d_hitgroupRecords;
    state.sbt.hitgroupRecordStrideInBytes = sizeof(HitGroupSbtRecord);
    state.sbt.hitgroupRecordCount = hitgroupRecords.size();
}

void Driver::init()
{
    createContext(m_state);

    size_t gb = 1024 * 1024 * 1024;
    m_state.arena.init(6 * gb);

    m_state.geometries = Container::createGeometryResults(m_state.context, m_state.arena);

    createModule(m_state);
    createProgramGroups(m_state);
    linkPipeline(m_state);
    createShaderBindingTable(m_state);

    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&d_params), sizeof(Params)));
    CHECK_CUDA(cudaDeviceSynchronize());
}

void Driver::launch(Cam cam, const std::string &exrFilename)
{
    std::vector<PtexTexture> textures;
    for (const auto &filename : Textures::textureFilenames) {
        PtexTexture texture(MOANA_ROOT + std::string("/island/") + filename);
        textures.push_back(texture);
    }

    CUstream stream;
    CHECK_CUDA(cudaStreamCreate(&stream));

    const int width = 1024;
    const int height = 429;

    Params params;

    const size_t outputBufferSizeInBytes = width * height * 3 * sizeof(float);
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&params.outputBuffer),
        outputBufferSizeInBytes
    ));
    CHECK_CUDA(cudaMemset(
        reinterpret_cast<void *>(params.outputBuffer),
        0,
        outputBufferSizeInBytes
    ));

    const size_t barycentricBufferSizeInBytes = width * height * 2 * sizeof(float);
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&params.barycentricBuffer),
        barycentricBufferSizeInBytes
    ));
    CHECK_CUDA(cudaMemset(
        reinterpret_cast<void *>(params.barycentricBuffer),
        0,
        barycentricBufferSizeInBytes
    ));

    const size_t idBufferSizeInBytes = width * height * sizeof(int) * 3;
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&params.idBuffer),
        idBufferSizeInBytes
    ));
    CHECK_CUDA(cudaMemset(
        reinterpret_cast<void *>(params.idBuffer),
        0,
        idBufferSizeInBytes
    ));

    const size_t colorBufferSizeInBytes = width * height * sizeof(float) * 3;
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&params.colorBuffer),
        colorBufferSizeInBytes
    ));
    CHECK_CUDA(cudaMemset(
        reinterpret_cast<void *>(params.colorBuffer),
        0,
        colorBufferSizeInBytes
    ));

    const size_t depthBufferSizeInBytes = width * height * sizeof(float);
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&params.depthBuffer),
        depthBufferSizeInBytes
    ));

    std::vector<float> depthBuffer(width * height, std::numeric_limits<float>::max());
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(params.depthBuffer),
        depthBuffer.data(),
        depthBufferSizeInBytes,
        cudaMemcpyHostToDevice
    ));

    // Camera camera(
    //     Vec3(60.f, 0.f, 700.f),
    //     Vec3(0.f, 80.f, 0.f),
    //     Vec3(0.f, 1.f, 0.f),
    //     33.f / 180.f * M_PI,
    //     Resolution{ width, height },
    //     false
    // );

    Scene scene(cam);
    Camera camera = scene.getCamera(width, height);

    params.camera = camera;

    for (auto [i, geometry] : enumerate(m_state.geometries)) {
        m_state.arena.restoreSnapshot(geometry.snapshot);

        params.handle = geometry.handle;
        CHECK_CUDA(cudaMemcpy(
            reinterpret_cast<void *>(d_params),
            &params,
            sizeof(params),
            cudaMemcpyHostToDevice
        ));

        CHECK_OPTIX(optixLaunch(
            m_state.pipeline,
            stream,
            d_params,
            sizeof(Params),
            &m_state.sbt,
            width,
            height,
            /*depth=*/1
        ));

        CHECK_CUDA(cudaDeviceSynchronize());
    }

    std::vector<float> outputBuffer(width * height * 3);
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(outputBuffer.data()),
        params.outputBuffer,
        outputBufferSizeInBytes,
        cudaMemcpyDeviceToHost
    ));
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<float> barycentricBuffer(width * height * 2);
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(barycentricBuffer.data()),
        params.barycentricBuffer,
        barycentricBufferSizeInBytes,
        cudaMemcpyDeviceToHost
    ));
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<int> idBuffer(width * height * 3);
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(idBuffer.data()),
        params.idBuffer,
        idBufferSizeInBytes,
        cudaMemcpyDeviceToHost
    ));
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<float> colorBuffer(width * height * 3);
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(colorBuffer.data()),
        params.colorBuffer,
        colorBufferSizeInBytes,
        cudaMemcpyDeviceToHost
    ));
    CHECK_CUDA(cudaDeviceSynchronize());

    ColorMap faceMap;
    ColorMap materialMap;
    std::vector<float> textureImage(width * height * 3, 0.f);
    std::vector<float> faceImage(width * height * 3, 0.f);
    std::vector<float> uvImage(width * height * 3, 0.f);
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            const int pixelIndex = 3 * (row * width + col);

            const int idIndex = 3 * (row * width + col);
            const int primitiveID = idBuffer[idIndex + 0];
            const int materialID = idBuffer[idIndex + 1];
            const bool useTexture = idBuffer[idIndex + 2];
            const int faceID = primitiveID / 2;

            const int barycentricIndex = 2 * (row * width + col);
            const float alpha = barycentricBuffer[barycentricIndex + 0];
            const float beta = barycentricBuffer[barycentricIndex + 1];

            float u, v;
            if (primitiveID % 2 == 0) {
                u = alpha + beta;
                v = beta;
            } else {
                u = alpha;
                v = alpha + beta;
            }
            uvImage[pixelIndex + 0] = u;
            uvImage[pixelIndex + 1] = v;
            uvImage[pixelIndex + 2] = materialID;

            if (materialID > 0) {
                float3 color = faceMap.get(faceID);
                faceImage[pixelIndex + 0] = color.x;
                faceImage[pixelIndex + 1] = color.y;
                faceImage[pixelIndex + 2] = color.z;
            }

            if (useTexture) {
                std::vector<TextureOffset> &offsetList = Textures::offsets[materialID];
                TextureOffset offset;
                bool found = false;
                for (const auto &offsetCandidate : offsetList) {
                    if (primitiveID <= offsetCandidate.endIndex) {
                        offset = offsetCandidate;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cout << "no texture? faceID: " << faceID << std::endl;
                }

                PtexTexture texture = textures[offset.textureIndex];

                int faceOffset = offset.startIndex / 2;
                Vec3 color = texture.lookup(
                    float2{ u, v },
                    faceID - faceOffset
                );
                textureImage[pixelIndex + 0] = color.x();
                textureImage[pixelIndex + 1] = color.y();
                textureImage[pixelIndex + 2] = color.z();
            } else if (materialID > 0) {
                float3 color = materialMap.get(materialID);

                textureImage[pixelIndex + 0] = colorBuffer[pixelIndex + 0];
                textureImage[pixelIndex + 1] = colorBuffer[pixelIndex + 1];
                textureImage[pixelIndex + 2] = colorBuffer[pixelIndex + 2];
            }

            textureImage[pixelIndex + 0] *= outputBuffer[pixelIndex + 0];
            textureImage[pixelIndex + 1] *= outputBuffer[pixelIndex + 1];
            textureImage[pixelIndex + 2] *= outputBuffer[pixelIndex + 2];
        }
    }

    // Image::save(
    //     width,
    //     height,
    //     outputBuffer,
    //     exrFilename
    // );

    Image::save(
        width,
        height,
        faceImage,
        "face-buffer_" + exrFilename
    );

    Image::save(
        width,
        height,
        textureImage,
        "texture-buffer_" + exrFilename
    );

    // Image::save(
    //     width,
    //     height,
    //     uvImage,
    //     "uv-buffer_" + exrFilename
    // );

    // CHECK_CUDA(cudaFree(reinterpret_cast<void *>(m_state.gasOutputBuffer)));
    CHECK_CUDA(cudaFree(params.outputBuffer));
}

}
