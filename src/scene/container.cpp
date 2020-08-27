#include "scene/container.hpp"

#include <memory>

#include "scene/dunes_a_geometry.hpp"
#include "scene/element.hpp"
#include "scene/hibiscus_geometry.hpp"
#include "scene/ias.hpp"
#include "scene/ironwood_a1_geometry.hpp"
#include "scene/mountain_a_geometry.hpp"
#include "scene/mountain_b_geometry.hpp"

namespace moana { namespace Container {

std::vector<GeometryResult> createGeometryResults(
    OptixDeviceContext context,
    ASArena &arena
) {
    std::vector<GeometryResult> geometries;

    std::unique_ptr<Element> elementPtrs[] = {
        std::make_unique<HibiscusElement>(),
        std::make_unique<MountainAElement>(),
        std::make_unique<MountainBElement>(),
        std::make_unique<DunesAElement>(),
        std::make_unique<IronwoodA1Element>(),
    };

    for (const auto &elementPtr : elementPtrs) {
        auto result = elementPtr->buildAcceleration(context, arena);

        geometries.push_back(result);
    }

    return geometries;
}

} }
