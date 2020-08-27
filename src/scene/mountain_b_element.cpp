#include "scene/mountain_b_element.hpp"

#include <iostream>
#include <string>

#include <cuda_runtime.h>

#include "assert_macros.hpp"
#include "moana/parsers/obj_parser.hpp"
#include "scene/archive.hpp"
#include "scene/gas.hpp"
#include "scene/ias.hpp"
#include "scene/instances_bin.hpp"

namespace moana {

GeometryResult MountainBElement::buildAcceleration(
    OptixDeviceContext context,
    ASArena &arena
) {
    const std::string moanaRoot = MOANA_ROOT;

    const std::string baseObj = moanaRoot + "/island/obj/isMountainB/isMountainB.obj";

    std::vector<OptixInstance> records;
    {
        std::cout << "Processing base obj: " << baseObj << std::endl;

        ObjParser objParser(baseObj);
        auto model = objParser.parse();

        const auto gasHandle = GAS::gasInfoFromObjResult(context, arena, model);

        float transform[12] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f
        };
        Instances instancesResult;
        instancesResult.transforms = transform;
        instancesResult.count = 1;

        IAS::createOptixInstanceRecords(
            context,
            records,
            instancesResult,
            gasHandle
        );
    }

    const std::vector<std::string> objPaths = {
        moanaRoot + "/island/obj/isMountainB/archives/xgFoliageB_treeMadronaBaked_canopyOnly_lo.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFoliageC_treeMadronaBaked_canopyOnly_lo.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFoliageA_treeMadronaBaked_canopyOnly_lo.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFoliageAd_treeMadronaBaked_canopyOnly_lo.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgBreadFruit_archiveBreadFruitBaked.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig3.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig14.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig17.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig8.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig12.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig2.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig1.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig16.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig15.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig6.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgCocoPalms_isPalmRig13.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0006_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0013_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0005_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0007_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0009_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0004_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0001_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0011_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0012_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0014_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0008_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0010_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0002_mod.obj",
        moanaRoot + "/island/obj/isMountainB/archives/xgFern_fern0003_mod.obj",
    };

    const std::vector<std::string> binPaths = {
        "../scene/mountainB-xgFoliageB_treeMadronaBaked_canopyOnly_lo.bin",
        "../scene/mountainB-xgFoliageC_treeMadronaBaked_canopyOnly_lo.bin",
        "../scene/mountainB-xgFoliageA_treeMadronaBaked_canopyOnly_lo.bin",
        "../scene/mountainB-xgFoliageAd_treeMadronaBaked_canopyOnly_lo.bin",
        "../scene/mountainB-xgBreadFruit_archiveBreadFruitBaked.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig3.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig14.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig17.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig8.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig12.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig2.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig1.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig16.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig15.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig6.bin",
        "../scene/mountainB-xgCocoPalms_isPalmRig13.bin",
        "../scene/mountainB-xgFern_fern0006_mod.bin",
        "../scene/mountainB-xgFern_fern0013_mod.bin",
        "../scene/mountainB-xgFern_fern0005_mod.bin",
        "../scene/mountainB-xgFern_fern0007_mod.bin",
        "../scene/mountainB-xgFern_fern0009_mod.bin",
        "../scene/mountainB-xgFern_fern0004_mod.bin",
        "../scene/mountainB-xgFern_fern0001_mod.bin",
        "../scene/mountainB-xgFern_fern0011_mod.bin",
        "../scene/mountainB-xgFern_fern0012_mod.bin",
        "../scene/mountainB-xgFern_fern0014_mod.bin",
        "../scene/mountainB-xgFern_fern0008_mod.bin",
        "../scene/mountainB-xgFern_fern0010_mod.bin",
        "../scene/mountainB-xgFern_fern0002_mod.bin",
        "../scene/mountainB-xgFern_fern0003_mod.bin",
    };

    Archive archive(binPaths, objPaths);
    archive.processRecords(context, arena, records);

    auto iasObjectHandle = IAS::iasFromInstanceRecords(context, arena, records);

    Snapshot snapshot = arena.createSnapshot();
    arena.releaseAll();
    return GeometryResult{
        iasObjectHandle,
        snapshot
    };
}

}