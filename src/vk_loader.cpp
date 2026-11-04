#include <string_view>
#include <vk_loader.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <filesystem>

#include "fmt/core.h"
#include "vk_types.h"
#include "vk_engine.h"
#include "fastgltf/types.hpp"
#include "fastgltf/util.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "stb_image.h"

#include "vk_descriptors.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <vulkan/vulkan_core.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image)
{
    AllocatedImage newImage = {};

    int width, height, nrChannels;

    auto upload = [&](const std::byte* byte, size_t size)
                    {
                        unsigned char* data = stbi_load_from_memory(
                                reinterpret_cast<const stbi_uc*>(byte), 
                                static_cast<int>(size), 
                                &width,
                                &height,
                                &nrChannels,
                                4
                        );

                        if(!data) return;

                        VkExtent3D imageSize;
                        imageSize.width = width;
                        imageSize.height = height;
                        imageSize.depth = 1;

                        
                        newImage = engine->create_image(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

                        stbi_image_free(data);
    };

    std::visit(
            fastgltf::visitor {
                [](auto& arg) { fmt::println("FALLBACK");},
                [&](fastgltf::sources::URI& filePath)
                {
                    assert(filePath.fileByteOffset == 0);
                    assert(filePath.uri.isLocalPath());
                    fmt::println("URI");
                    const std::string path(filePath.uri.path().begin(),
                            filePath.uri.path().end()); // ???
                    
                    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
                    if(data)
                    {
                        VkExtent3D imagesSize;
                        imagesSize.width = width;
                        imagesSize.height = height;
                        imagesSize.depth = 1;

                        newImage = engine->create_image(data, imagesSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

                        stbi_image_free(data);
                    }
                },
                [&](fastgltf::sources::Vector& vector)
                {
                    upload(vector.bytes.data(), vector.bytes.size());
                },
                [&](fastgltf::sources::Array& array)
                {
                    upload(array.bytes.data(), array.bytes.size());
                },
                [&](fastgltf::sources::ByteView& b)
                {
                    upload(b.bytes.data(), b.bytes.size());
                },
                [&](fastgltf::sources::BufferView& view)
                {
                    auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                    auto& buffer = asset.buffers[bufferView.bufferIndex];


                    std::visit(fastgltf::visitor {
                        [](auto& arg) {},
                        [&](fastgltf::sources::Vector& vector)
                        {
                            upload(vector.bytes.data() + bufferView.byteOffset, bufferView.byteLength);
                        },
                        [&](fastgltf::sources::Array& array)
                        {
                            upload(array.bytes.data() + bufferView.byteOffset, bufferView.byteLength);
                        },
                        [&](fastgltf::sources::ByteView& view)
                        {
                            upload(view.bytes.data() + bufferView.byteOffset, bufferView.byteLength);
                        }
                    },buffer.data);
                },
            },image.data);

    if(newImage.image == VK_NULL_HANDLE)
        return {};
    else
        return newImage;
}

std::shared_ptr<LoadedGLTF> loadGltf(VulkanEngine* engine, std::string_view filePath)
{
    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    scene->creator = engine;
    LoadedGLTF& file = *scene.get();
    fmt::println("Scene get");

    static constexpr auto supportedExtensions = fastgltf::Extensions::None;
    fastgltf::Parser parser(supportedExtensions);

    constexpr auto gltfOptions = 
        fastgltf::Options::DontRequireValidAssetMember | 
        fastgltf::Options::AllowDouble | 
        fastgltf::Options::LoadExternalBuffers | 
        fastgltf::Options::LoadExternalImages | 
        fastgltf::Options::GenerateMeshIndices;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(filePath);
    if (!gltfFile) {
        fmt::print(stderr, "Failed to open glTF file: {}\n", fastgltf::to_underlying(gltfFile.error()));
        return nullptr;
    }

    std::filesystem::path path = filePath;

    auto load = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
    
    if (!load) {
        fmt::print(stderr, "Failed to load glTF: {}\n", fastgltf::to_underlying(load.error()));
        return nullptr;
    }
    

    fastgltf::Asset gltf = std::move(load.get());

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };

    file.descriptorPool.init(engine->_device, gltf.materials.size(), sizes);

    // Load samplers
    for( fastgltf::Sampler& sampler : gltf.samplers)
    {
        VkSamplerCreateInfo sampl = {};
        sampl.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampl.pNext = nullptr;
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        vkCreateSampler(engine->_device, &sampl, nullptr, &newSampler);

        file.samplers.push_back(newSampler);
    }

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<AllocatedImage> images;
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    for(fastgltf::Image& image : gltf.images)
    {
        std::optional<AllocatedImage> img = load_image(engine, gltf, image);

        if(img.has_value())
        {
            images.push_back(*img);
            file.images[image.name.c_str()] = *img;
        }
        else
        {
            images.push_back(engine->_errorCheckerboardImage);
            fmt::println("GLTF failed to load texture {}",image.name);
        }
    }

    file.materialDataBuffer = engine->create_buffer(
            sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );

    int data_index = 0;
    GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants = static_cast<GLTFMetallic_Roughness::MaterialConstants*>(file.materialDataBuffer.info.pMappedData);

    for(fastgltf::Material& mat : gltf.materials)
    {
        std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
        materials.push_back(newMat);
        file.materials[mat.name.c_str()] = newMat;

        GLTFMetallic_Roughness::MaterialConstants constants;
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        sceneMaterialConstants[data_index] = constants;

        MaterialPass passType = MaterialPass::MainColor;
        if(mat.alphaMode == fastgltf::AlphaMode::Blend)
            passType = MaterialPass::Transparent;

        GLTFMetallic_Roughness::MaterialResources materialResources;

        materialResources.colorImage = engine->_whiteImage;
        materialResources.colorSampler = engine->_defaultSamplerLinear;
        materialResources.metalRoughImage = engine->_whiteImage;
        materialResources.metalRoughSampler = engine->_defaultSamplerLinear;

        materialResources.dataBuffer = file.materialDataBuffer.buffer;
        materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);

        if(mat.pbrData.baseColorTexture.has_value())
        {
            auto& tex = gltf.textures[mat.pbrData.baseColorTexture->textureIndex];
            if(tex.imageIndex.has_value() && tex.imageIndex.value() < images.size())
                materialResources.colorImage = images[gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value()];

            if(tex.samplerIndex.has_value() && tex.samplerIndex.value() < file.samplers.size())
                materialResources.colorSampler = file.samplers[gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value()];
        }

        newMat->data = engine->metalRoughMaterial.write_material(engine->_device, passType, materialResources, file.descriptorPool);
        data_index++;
    }

    fmt::println("Materials loaded succesfully");

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for(fastgltf::Mesh& mesh : gltf.meshes)
    {
        std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();

        meshes.push_back(newMesh);

        file.meshes[mesh.name.c_str()] = newMesh;
        newMesh->name = mesh.name;

        indices.clear();
        vertices.clear();

        for(auto&& p : mesh.primitives)
        {
            GeoSurface newSurface;
            newSurface.startIndex = static_cast<uint32_t>(indices.size());
            newSurface.count = static_cast<uint32_t>(gltf.accessors[p.indicesAccessor.value()].count);

            size_t initial_vtx = vertices.size();

            // Load indexes
            {
                fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, 
                [&](std::uint32_t idx) {
                    indices.push_back(idx + initial_vtx);
                });
            }

            // Load vertex positions
            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, 
                [&](glm::vec3 v, size_t index){
                    Vertex vx;
                    vx.position = v;
                    vx.normal = { 1, 0, 0 };
                    vx.color = glm::vec4(1.0f);
                    vx.uv_x = 0;
                    vx.uv_y = 0;
                    vertices[initial_vtx + index] = vx;
                });
            }

            // load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) 
            {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).accessorIndex],
                [&](glm::vec3 v, size_t index) {
                    vertices[initial_vtx + index].normal = v;
                });
            }

            // load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) 
            {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).accessorIndex],
                [&](glm::vec2 v, size_t index) {
                    vertices[initial_vtx + index].uv_x = v.x;
                    vertices[initial_vtx + index].uv_y = v.y;
                });
            }

            // load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) 
            {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).accessorIndex],
                [&](glm::vec4 v, size_t index) {
                    vertices[initial_vtx + index].color = v;
                });
            }

            if (p.materialIndex.has_value())
                newSurface.material = materials[p.materialIndex.value()];
            else
                newSurface.material = materials[0];
    

            newMesh->surfaces.push_back(newSurface);
        }

        newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
    }

    fmt::println("Meshes loaded succesfully");

    for(fastgltf::Node& node : gltf.nodes)
    {
        std::shared_ptr<Node> newNode;

        if(node.meshIndex.has_value())
        {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
        } 
        else 
        {
            newNode = std::make_shared<Node>();
        }


        nodes.push_back(newNode);
        file.nodes[node.name.c_str()] = newNode;

        fastgltf::math::mat<float, 4, 4> transform = fastgltf::getTransformMatrix(node);
        
        newNode->localTransform = glm::make_mat4x4(transform.data());
    }

    fmt::println("Nodes loaded successfully");

    for(int i = 0; i < gltf.nodes.size(); i++)
    {
        fastgltf::Node& node = gltf.nodes[i];
        std::shared_ptr<Node>& sceneNode = nodes[i];

        for(auto& c : node.children)
        {
            sceneNode->children.push_back(nodes[c]);
            nodes[c]->parent = sceneNode;
        }
    }


    for(auto& node : nodes)
    {
        if(node->parent.lock() == nullptr)
        {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4(1.0f));
        }
    }


    return scene;
}

VkFilter extract_filter(fastgltf::Filter filter)
{
    switch(filter)
    {
        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::NearestMipMapLinear:
            return VK_FILTER_NEAREST;

        case fastgltf::Filter::Linear:
        case fastgltf::Filter::LinearMipMapNearest:
        case fastgltf::Filter::LinearMipMapLinear:
        default:
            return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter)
    {
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::LinearMipMapNearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;

        case fastgltf::Filter::NearestMipMapLinear:
        case fastgltf::Filter::LinearMipMapLinear:
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}


void LoadedGLTF::clearAll()
{
    VkDevice dv = creator->_device;

    descriptorPool.destroy_pools(dv);
    creator->destroy_buffer(materialDataBuffer);

    for(auto& [k, v] : meshes)
    {
        creator->destroy_buffer(v->meshBuffers.indexBuffer);
        creator->destroy_buffer(v->meshBuffers.vertexBuffer);
    }

    for(auto& [k, v] : images)
    {
        if(v.image == creator->_errorCheckerboardImage.image)
            continue;

        creator->destroy_image(v);
    }

    for(auto& sampler : samplers)
    {
        vkDestroySampler(dv, sampler, nullptr);
    }
}
