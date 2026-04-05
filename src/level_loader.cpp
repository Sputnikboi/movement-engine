#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf.h"
#include "level_loader.h"

#include "vendor/HandmadeMath.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// ============================================================
//  Helper: read a float component from a cgltf accessor
// ============================================================

static bool read_float(const cgltf_accessor* acc, cgltf_size index,
                       cgltf_float* out, cgltf_size components)
{
    return cgltf_accessor_read_float(acc, index, out, components);
}

// ============================================================
//  Helper: compute a 4x4 transform for a node (local * parent chain)
// ============================================================

static void node_world_transform(const cgltf_node* node, float out[16]) {
    cgltf_node_transform_world(node, out);
}

// Transform a vec3 point by a 4x4 column-major matrix
static void transform_point(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0]*in[0] + m[4]*in[1] + m[8]*in[2]  + m[12];
    out[1] = m[1]*in[0] + m[5]*in[1] + m[9]*in[2]  + m[13];
    out[2] = m[2]*in[0] + m[6]*in[1] + m[10]*in[2] + m[14];
}

// Transform a vec3 direction (no translation) by upper-left 3x3
static void transform_dir(const float m[16], const float in[3], float out[3]) {
    out[0] = m[0]*in[0] + m[4]*in[1] + m[8]*in[2];
    out[1] = m[1]*in[0] + m[5]*in[1] + m[9]*in[2];
    out[2] = m[2]*in[0] + m[6]*in[1] + m[10]*in[2];
    // Normalize
    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-6f) { out[0] /= len; out[1] /= len; out[2] /= len; }
}

// ============================================================
//  Extract mesh data from a single cgltf primitive
// ============================================================

static void extract_primitive(const cgltf_primitive* prim,
                              const float transform[16],
                              const float default_color[3],
                              Mesh& out)
{
    if (prim->type != cgltf_primitive_type_triangles)
        return;

    // Find accessors
    const cgltf_accessor* pos_acc    = nullptr;
    const cgltf_accessor* norm_acc   = nullptr;
    const cgltf_accessor* color_acc  = nullptr;

    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        if (prim->attributes[a].type == cgltf_attribute_type_position)
            pos_acc = prim->attributes[a].data;
        else if (prim->attributes[a].type == cgltf_attribute_type_normal)
            norm_acc = prim->attributes[a].data;
        else if (prim->attributes[a].type == cgltf_attribute_type_color)
            color_acc = prim->attributes[a].data;
    }

    if (!pos_acc) return;

    uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size());

    // Read vertices
    for (cgltf_size v = 0; v < pos_acc->count; v++) {
        Vertex3D vert{};

        // Position (transform to world space)
        float local_pos[3] = {0, 0, 0};
        read_float(pos_acc, v, local_pos, 3);
        transform_point(transform, local_pos, vert.pos);

        // Normal (transform direction only)
        if (norm_acc) {
            float local_norm[3] = {0, 1, 0};
            read_float(norm_acc, v, local_norm, 3);
            transform_dir(transform, local_norm, vert.normal);
        } else {
            vert.normal[0] = 0; vert.normal[1] = 1; vert.normal[2] = 0;
        }

        // Color
        if (color_acc) {
            float c[4] = {1, 1, 1, 1};
            cgltf_size comp = cgltf_num_components(color_acc->type);
            read_float(color_acc, v, c, comp);
            // Vertex colors from Blender are often in linear space already
            vert.color[0] = c[0];
            vert.color[1] = c[1];
            vert.color[2] = c[2];
        } else {
            vert.color[0] = default_color[0];
            vert.color[1] = default_color[1];
            vert.color[2] = default_color[2];
        }

        out.vertices.push_back(vert);
    }

    // Read indices
    if (prim->indices) {
        for (cgltf_size i = 0; i < prim->indices->count; i++) {
            uint32_t idx = static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, i));
            out.indices.push_back(base_vertex + idx);
        }
    } else {
        // Non-indexed: generate sequential indices
        for (cgltf_size i = 0; i < pos_acc->count; i++) {
            out.indices.push_back(base_vertex + static_cast<uint32_t>(i));
        }
    }
}

// ============================================================
//  Case-insensitive name check
// ============================================================

static bool name_matches(const char* name, const char* target) {
    if (!name) return false;
    for (int i = 0; name[i] && target[i]; i++) {
        char a = name[i];
        char b = target[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    // Check both strings ended (allow name to have trailing stuff like ".001")
    return true;
}

static bool name_starts_with(const char* name, const char* prefix) {
    if (!name) return false;
    for (int i = 0; prefix[i]; i++) {
        char a = name[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

// ============================================================
//  Recursively process scene nodes
// ============================================================

static void process_node(const cgltf_node* node, LevelData& out) {
    float transform[16];
    node_world_transform(node, transform);

    // Log all node names so we can debug what Blender exported
    if (node->name) {
        fprintf(stdout, "  node: \"%s\"%s%s\n",
                node->name,
                node->mesh ? " [mesh]" : "",
                (!node->mesh && !node->children_count) ? " [empty]" : "");
    }

    // Check for spawn point (Empty named "spawn" or "Spawn" etc.)
    if (name_starts_with(node->name, "spawn")) {
        // Extract world position from the transform matrix (column 3)
        out.spawn_pos = HMM_V3(transform[12], transform[13], transform[14]);
        out.has_spawn = true;
        fprintf(stdout, "  Found spawn point: (%.1f, %.1f, %.1f) [node: %s]\n",
                out.spawn_pos.X, out.spawn_pos.Y, out.spawn_pos.Z, node->name);
    }

    if (node->mesh) {
        // "Ladder" → trigger volume (invisible). "VLadder" → visual only (rendered normally).
        bool is_ladder = name_starts_with(node->name, "ladder")
                      && !name_starts_with(node->name, "vladder");

        // Choose target mesh: ladder nodes go to invisible ladder_mesh
        Mesh& target = is_ladder ? out.ladder_mesh : out.mesh;
        uint32_t idx_before = static_cast<uint32_t>(target.indices.size());

        for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
            float color[3] = {0.5f, 0.5f, 0.5f};

            const cgltf_primitive* prim = &node->mesh->primitives[p];
            if (prim->material) {
                if (prim->material->has_pbr_metallic_roughness) {
                    const float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
                    color[0] = bc[0];
                    color[1] = bc[1];
                    color[2] = bc[2];
                }
            }

            extract_primitive(prim, transform, color, target);
        }

        // Record sub-mesh range if node has a name
        uint32_t idx_after = static_cast<uint32_t>(target.indices.size());
        if (node->name && idx_after > idx_before) {
            SubMeshRange range;
            snprintf(range.name, sizeof(range.name), "%s", node->name);
            range.index_start = idx_before;
            range.index_count = idx_after - idx_before;

            if (is_ladder)
                out.ladder_submeshes.push_back(range);
            else
                out.submeshes.push_back(range);
        }
    }

    for (cgltf_size c = 0; c < node->children_count; c++) {
        process_node(node->children[c], out);
    }
}

// ============================================================
//  Public API
// ============================================================

LevelData load_level_gltf(const std::string& path) {
    LevelData result;

    cgltf_options options{};
    cgltf_data* data = nullptr;

    cgltf_result parse_result = cgltf_parse_file(&options, path.c_str(), &data);
    if (parse_result != cgltf_result_success) {
        fprintf(stderr, "Failed to parse glTF: %s (error %d)\n", path.c_str(), parse_result);
        return result;
    }

    cgltf_result load_result = cgltf_load_buffers(&options, data, path.c_str());
    if (load_result != cgltf_result_success) {
        fprintf(stderr, "Failed to load glTF buffers: %s (error %d)\n", path.c_str(), load_result);
        cgltf_free(data);
        return result;
    }

    cgltf_result validate_result = cgltf_validate(data);
    if (validate_result != cgltf_result_success) {
        fprintf(stderr, "Warning: glTF validation failed for %s (error %d)\n", path.c_str(), validate_result);
    }

    for (cgltf_size s = 0; s < data->scenes_count; s++) {
        const cgltf_scene* scene = &data->scenes[s];
        for (cgltf_size n = 0; n < scene->nodes_count; n++) {
            process_node(scene->nodes[n], result);
        }
    }

    cgltf_free(data);

    if (result.mesh.vertices.empty()) {
        fprintf(stderr, "Warning: no mesh data found in %s\n", path.c_str());
    } else {
        fprintf(stdout, "Loaded level: %s (%zu vertices, %zu indices, %zu triangles)\n",
                path.c_str(),
                result.mesh.vertices.size(),
                result.mesh.indices.size(),
                result.mesh.indices.size() / 3);
        if (!result.has_spawn)
            fprintf(stdout, "  No spawn point found — add an Empty named 'spawn' in Blender. Starting in noclip.\n");
    }

    return result;
}
