// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include "glm/ext/vector_float4.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan_core.h>
#include "Material.h"
#include "vk_descriptors.h"

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (static_cast<int>(err) < 0) {                                \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
        else if(static_cast<int>(err) != 0) {                            \
            fmt::println("Detected Vulkan non-0 return value: {}", string_VkResult(err)); \
        }                                                               \
    } while (0)


struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        // Execute function in reverse
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
        {
            (*it)();
        }

        deletors.clear();
    }
};

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;             // buffer handle
    VmaAllocation allocation;    // actual buffer memory block
    VmaAllocationInfo info;
};

struct Vertex
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddres;
};

struct GPUDrawPushConstants
{
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};

struct GPUSceneData
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj; // Redundant
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
};

enum class MaterialPass : uint8_t
{
    MainColor,
    Transparent,
    Other
};

struct MaterialPipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct MaterialInstance
{
    MaterialPipeline* pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

struct GLTFMaterial
{
    MaterialInstance data;
};

struct RenderObject
{
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance* material;

    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};


struct DrawContext
{
    std::vector<RenderObject> OpaqueSurfaces;
};


class IRenderable
{
public:
    virtual ~IRenderable() = default;
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};
class Node : public IRenderable
{
public:
    virtual ~Node() override = default;

    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;
    
    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    // Propagate movement to child nodes
    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for(auto c : children)
            c->refreshTransform(worldTransform);
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override
    {
        for(auto& c : children)
            c->Draw(topMatrix, ctx);
    }

};

struct GeoSurface
{
    uint32_t startIndex;
    uint32_t count;
    std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset
{
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};



class MeshNode : public Node
{
public:
    virtual ~MeshNode() override = default;
    std::shared_ptr<MeshAsset> mesh;

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override
    {
        if(!mesh) return;
        glm::mat4 nodeMatrix = topMatrix * worldTransform;

        for(auto& s : mesh->surfaces)
        {
            RenderObject def;
            def.indexCount = s.count;
            def.firstIndex = s.startIndex;
            def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
            if (s.material)
                def.material = &s.material->data;
            else
                def.material = nullptr;

            def.transform = nodeMatrix;
            def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddres;

            ctx.OpaqueSurfaces.push_back(def);
        }

        Node::Draw(topMatrix, ctx);

    }
};


