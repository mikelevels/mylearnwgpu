#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>

bool ResourceManager::loadGeometry(
    const std::filesystem::path& path,
    std::vector<float>& pointData,
    std::vector<uint16_t>& indexData) {
    std::ifstream file(path);

    if (!file.is_open()) {
        return false;
    }

    pointData.clear();
    indexData.clear();

    enum class Section {
        None,
        Points,
        Indices,
    };

    Section currentSection = Section::None;

    float value;
    uint16_t index;
    std::string line;
    while (!file.eof()) {
        getline(file, line);

        //overcome the `CRLF` problem
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line == "[points]") {
            currentSection = Section::Points;
        }
        else if (line == "[indices]") {
            currentSection = Section::Indices;
        }
        else if (line[0] == '#' || line.empty()) {
            //DO NOTHING THE FILE CONTAINS A COMMENT
        }
        else if (currentSection == Section::Points) {
            std::istringstream iss(line);
            //Get x,y,r,g,b
            for (int i = 0; i < 5; ++i) {
                iss >> value;
                pointData.push_back(value);
            }
        }
        else if (currentSection == Section::Indices) {
            std::istringstream iss(line);
            //Get the corners of each triangle...#0 #1 #2
            for (int i = 0; i < 3; ++i) {
                iss >> index;
                indexData.push_back(index);
            }
        }
    }
    return true;
}


    wgpu::ShaderModule ResourceManager::loadShaderModule(const std::filesystem::path& path, wgpu::Device device) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return nullptr;
        }
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        std::string shaderSource(size, ' ');
        file.seekg(0);
        file.read(shaderSource.data(), size);

        wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc{};
        shaderCodeDesc.chain.next = nullptr;
        shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource.c_str();

        wgpu::ShaderModuleDescriptor shaderDesc{};

#ifdef WEBGPU_BACKEND_WGPU
        shaderDesc.hintCount = 0;
        shaderDesc.hints = nullptr;
#endif // WEBGPU_BACKEND_WGPU
        shaderDesc.nextInChain = &shaderCodeDesc.chain;

        return device.createShaderModule(shaderDesc);
    }