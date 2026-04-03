#pragma once

#include <cstdint>
#include <vector>

struct Vertex3D {
    float pos[3];
    float normal[3];
    float color[3];
};

struct Mesh {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
};

// Builds a test level: floor, walls, ramps, platforms — everything you need
// to fly around and later test movement physics against.
Mesh create_test_level();
