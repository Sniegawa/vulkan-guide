#pragma once

#include "fastgltf/types.hpp"
#include <string_view>
#include <vk_types.h>
#include <unordered_map>


class VulkanEngine;
class LoadedGLTF : public IRenderable
{
public:
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, AllocatedImage> images;
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    std::vector<std::shared_ptr<Node>> topNodes;

    std::vector<VkSampler> samplers;

    DescriptorAllocatorGrowable descriptorPool;

    AllocatedBuffer materialDataBuffer;

    VulkanEngine* creator; 
    
    ~LoadedGLTF() { clearAll(); };

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override
    {
        for(auto& n : topNodes)
        {
            n->Draw(topMatrix, ctx);
        }
    }

private:
    void clearAll();
};

std::shared_ptr<LoadedGLTF> loadGltf(VulkanEngine* engine,  std::string_view filePath);

VkFilter extract_filter(fastgltf::Filter filter);

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);
