#include "moana/driver.hpp"

#include <iomanip>
#include <iostream>

#include <cuda_runtime.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include "assert_macros.h"

namespace moana {

struct OptixState {
    OptixDeviceContext context;
    OptixTraversableHandle gasHandle;
};

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

static void createGeometry(OptixState &state)
{
    OptixAccelBuildOptions accelOptions = {};
    accelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE; // no build flags
    accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD; // no updates

    // Setup geometry buffer
    float vertices[] = {
        0.f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        1.f, 0.f, 0.f,
    };

    CUdeviceptr d_vertices = 0;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void **>(&d_vertices), sizeof(vertices)));
    CHECK_CUDA(cudaMemcpy(
        reinterpret_cast<void *>(d_vertices),
        vertices,
        sizeof(vertices),
        cudaMemcpyHostToDevice
    ));

    // Setup build input
    uint32_t inputFlags[] = { OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT };
    OptixBuildInput triangleInput = {};
    triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    triangleInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    triangleInput.triangleArray.numVertices = static_cast<uint32_t>(3);
    triangleInput.triangleArray.vertexBuffers = &d_vertices;
    triangleInput.triangleArray.flags = inputFlags;
    triangleInput.triangleArray.numSbtRecords = 1;

    // Calculate, allocate, and copy to device memory
    OptixAccelBufferSizes gasBufferSizes;
    CHECK_OPTIX(optixAccelComputeMemoryUsage(
        state.context,
        &accelOptions,
        &triangleInput,
        1, // build input count
        &gasBufferSizes
    ));

    CUdeviceptr d_tempBufferGas;
    CUdeviceptr d_gasOutputBuffer;
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&d_tempBufferGas),
        gasBufferSizes.tempSizeInBytes
    ));
    CHECK_CUDA(cudaMalloc(
        reinterpret_cast<void **>(&d_gasOutputBuffer),
        gasBufferSizes.outputSizeInBytes
    ));

    CHECK_OPTIX(optixAccelBuild(
        state.context,
        0, // default CUDA stream
        &accelOptions,
        &triangleInput,
        1, // build input count
        d_tempBufferGas,
        gasBufferSizes.tempSizeInBytes,
        d_gasOutputBuffer,
        gasBufferSizes.outputSizeInBytes,
        &state.gasHandle,
        nullptr, 0 // emitted property params
    ));

    CHECK_CUDA(cudaFree(reinterpret_cast<void *>(d_tempBufferGas)));
    CHECK_CUDA(cudaFree(reinterpret_cast<void *>(d_vertices)));
}

void Driver::init()
{
    OptixState state;

    createContext(state);
    createGeometry(state);
}

}