#include "ResourceManager.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

bool ResourceManager::loadGeometry(
    const std::filesystem::path& path,
    std::vector<float>& pointData,
    std::vector<uint16_t>& indexData,
    int dimensions) {
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
    // Loop on getline() itself rather than on `!file.eof()`: getline returns
    // false as soon as there is nothing left to read, whereas eof() only
    // becomes true AFTER a read has already failed, which would make us
    // process one extra (empty) line at the end.
    while (getline(file, line)) {

        //overcome the `CRLF` problem
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Position of the first character that is not a space or a tab.
        // npos means the line is blank (empty, or only spaces/tabs).
        // Step050: the guide's pyramid.txt ends with a line containing a
        // single space. The old check (line.empty()) did not see it as blank,
        // so the guide's parser silently added a fake triangle (4, 4, 4) and
        // our stricter parser refused the whole file.
        size_t firstChar = line.find_first_not_of(" \t");

        if (line == "[points]") {
            currentSection = Section::Points;
        }
        else if (line == "[indices]") {
            currentSection = Section::Indices;
        }
        else if (firstChar == std::string::npos || line[firstChar] == '#') {
            //DO NOTHING THE LINE IS BLANK OR A COMMENT
        }
        else if (currentSection == Section::Points) {
            std::istringstream iss(line);
            //Get x,y(,z),r,g,b: `dimensions` coordinates then 3 color values
            for (int i = 0; i < dimensions + 3; ++i) {
                // `iss >> value` evaluates to false if the next word is not a
                // number or if the line has too few values. Before, we
                // pushed whatever was left in `value` and silently loaded a
                // broken mesh. Now we report the file as invalid instead.
                if (!(iss >> value)) {
                    return false;
                }
                pointData.push_back(value);
            }
        }
        else if (currentSection == Section::Indices) {
            std::istringstream iss(line);
            //Get the corners of each triangle...#0 #1 #2
            for (int i = 0; i < 3; ++i) {
                if (!(iss >> index)) {
                    return false;
                }
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
        // BUG FIX: we used to measure the file with tellg() and read that many
        // characters, but on Windows a text-mode read turns each "\r\n" into
        // "\n", so fewer characters arrive than measured (the rest stayed as
        // padding spaces). Reading until the end of the stream gets exactly
        // what is there.
        std::string shaderSource(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

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