#include <cassert>
#include <cstdint>
#include <memory>
#include <vk_loader.h>

#include "fastgltf/util.hpp"
#include "fmt/os.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "stb_image.h"
#include <iostream>
#include <vk_loader.h>

#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>



std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath)
{
    std::cout << "Loading GLTF: " << filePath << std::endl;

    static constexpr auto supportedExtensions = fastgltf::Extensions::None;

    fastgltf::Parser parser(supportedExtensions);

    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;

    
    auto gltfFile = fastgltf::MappedGltfFile::FromPath(filePath);

    auto asset = parser.loadGltfBinary(gltfFile.get(), filePath.parent_path(), gltfOptions);
    
    if(!asset)
    {
        fmt::print("Failed to load glTF: {} \n", fastgltf::to_underlying(asset.error()));
        return {};
    }

    std::vector<std::shared_ptr<MeshAsset>> meshes;

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for(auto& mesh : asset->meshes)
    {
        MeshAsset newMesh;

        newMesh.name = mesh.name;

        // We don't want to merge mesh buffers
        indices.clear();
        vertices.clear();

        for(auto&& p : mesh.primitives)
        {
            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            newSurface.count = (uint32_t)asset->accessors[p.indicesAccessor.value()].count;

            size_t initial_vtx = vertices.size();

            {
                auto& indexAccessor = asset->accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(asset.get(), indexAccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + initial_vtx);
                    });
            }

            // Load vertex positions
            {
                auto& posAccessor = asset->accessors[p.findAttribute("POSITION")->accessorIndex];

                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 }; // Normals are read later on
                        newvtx.color = glm::vec4 { 1.0f }; // Same for color
                        newvtx.uv_x = 0; // And UV's
                        newvtx.uv_y = 0;
                        vertices[initial_vtx + index] = newvtx;
                    }); 
            }

            // Load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if(normals != p.attributes.end())
            {
                auto& normAccessor = asset->accessors[(*normals).accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), normAccessor, 
                [&](glm::vec3 v, size_t index) {
                    vertices[initial_vtx + index].normal = v;
                });
            }

            // Load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if(uv != p.attributes.end())
            {
                auto& uvAccessor = asset->accessors[(*uv).accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), uvAccessor, 
                [&](glm::vec2 v, size_t index){
                   vertices[initial_vtx + index].uv_x = v.x;
                   vertices[initial_vtx + index].uv_y = v.y;
                });
            }

            // Load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end())
            {
                auto& colAccessor = asset->accessors[(*colors).accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), colAccessor, 
                    [&](glm::vec4 v, size_t index) {
                        vertices[initial_vtx + index].color = v;
                    });
            }
            newMesh.surfaces.push_back(newSurface);
        }

        constexpr bool OverrideColors = true;
        if(OverrideColors)
        {
            for(Vertex& vtx : vertices)
            {
                vtx.color = glm::vec4(vtx.normal, 1.0f);
            }
        }

        newMesh.meshBuffers = engine->uploadMesh(indices,vertices);

        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
    }

    return meshes;
}
