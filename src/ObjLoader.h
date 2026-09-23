#pragma once
#include "Object.hpp"
#include "Material.hpp"
#include <cstdio>
#include <cstring>
#include <array>
#include <map>
#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

namespace Loader
{
// Scoped to namespace Loader so the class `Renderer` (a member of namespace
// `Renderer`) doesn't leak into the global namespace, which on MSVC would
// make `Renderer::Object` ambiguous (C2872) in any TU including this header.
using namespace Renderer;
    [[maybe_unused]] static Texture* LoadRawBMP(const char *fileName) {
        FILE *file = fopen(fileName, "rb");
        if (!file)
        {
            printf("Error: Unable to open file %s\n", fileName);
            return nullptr;
        }

        // Make sure the file is large enough to even possibly be a BMP
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        if (fileSize < 54)
        {
            printf("Error: File is too small to be a BMP\n");
            fclose(file);
            return nullptr;
        }
        fseek(file, 0, SEEK_SET);

        // Read the header
        uint8_t header[54];
        fread(header, sizeof(uint8_t), 54, file);

        // Make sure that it's a 16-bit image
        if (header[28] != 16)
        {
            printf("Error: Only 16-bit BMP images are supported\n");
            fclose(file);
            return nullptr;
        }

        // Extract image dimensions
        int width = header[18] | (header[19] << 8);
        int height = header[22] | (header[23] << 8);

        // Read the image data
        uint16_t *data = new uint16_t[width * height];
        fread(data, sizeof(uint16_t), width * height, file);

        auto texture = new Texture(width, height, data, false, 0, false, TextureAddressMode::WRAP);

        // Close the file
        fclose(file);
        return texture;
    }

    static void LoadMtlData(const char *fileData, std::vector<Material*> *materialLibrary, std::vector<Texture*> *textureLibrary) {
        const char *line = fileData;
        Material *currentMaterial = nullptr;

        while (*line)
        {
            if (line[0] == 'n' && line[1] == 'e')
            {
                char materialName[256];
                if (sscanf(line, "newmtl %255s", materialName) != 1) break;
                currentMaterial = new Material();
                currentMaterial->name = strdup(materialName);  // Use strdup to allocate memory for the name
                materialLibrary->push_back(currentMaterial);
                printf("Loaded material %s\n", currentMaterial->name);
            }
            else if (currentMaterial != nullptr) // Ensure currentMaterial is not null before assigning properties
            {
                if (line[0] == 'K' && line[1] == 'd')
                {
                    float r=1, g=1, b=1;
                    sscanf(line, "Kd %f %f %f", &r, &g, &b);
                    r=std::clamp(r,0.f,1.f);g=std::clamp(g,0.f,1.f);b=std::clamp(b,0.f,1.f);
                    uint8_t rInt = static_cast<uint8_t>(r * 255);
                    uint8_t gInt = static_cast<uint8_t>(g * 255);
                    uint8_t bInt = static_cast<uint8_t>(b * 255);
                    currentMaterial->color = (rInt >> 3) << 11 | (gInt >> 2) << 5 | (bInt >> 3);
                }
                else if (line[0] == 'd')
                {
                    float alpha=1;
                    sscanf(line, "d %f", &alpha);
                    alpha=std::clamp(alpha,0.f,1.f);
                    currentMaterial->alpha = static_cast<uint8_t>(alpha * 255);
                }
                else if (std::strncmp(line,"map_Kd ",7)==0)
                {
                    // Names may contain spaces. Unresolved maps retain the
                    // material colour instead of creating a null pixel texture.
                    char textureName[256]={};
                    if (sscanf(line,"map_Kd %255[^\r\n]",textureName)==1 && textureLibrary) {
                        for(auto* texture : *textureLibrary) {
                            if(texture && texture->name && std::strcmp(texture->name,textureName)==0) {
                                currentMaterial->diffuseMap=texture;
                                break;
                            }
                        }
                    }
                }
            }

            // Move to the next line
            while (*line && *line != '\n')
                line++;
            if (*line)
                line++;
        }
    }

    // `scale` is a runtime multiplier applied to every loaded vertex
    // position on top of the engine's internal FIXED_POINT_SCALE/8
    // baseline. Use it to enlarge content authored at "unit = 1 world
    // unit" so the renderer runs at a coarser world grid — useful at
    // high screen resolutions where each world unit is several pixels
    // and the renderer's integer rotation chain produces visible vertex
    // wobble. UVs and normals are not affected. Default 1.0f preserves
    // the historical behaviour.
    static Object *LoadFromObjData(const char *fileData, Material *defaultMaterial = nullptr, std::vector<Material*> *materialLibrary = nullptr, float scale = 1.0f)
    {
        Object *obj = new Object();
        const char *line = fileData;
        std::vector<float> tempVertices;
        std::vector<float> tempUVs;
        std::vector<float> tempNormals;
        std::map<std::array<int,3>,uint16_t> vertexLookup;
        Material *currentMaterial = defaultMaterial;

        const float SCALE = (FIXED_POINT_SCALE / 8) * scale;

        while (*line)
        {
            //Custom: "mr" for material reset - revert to default material
            if (line[0] == 'm' && line[1] == 'd')
            {
                currentMaterial = defaultMaterial;
            }
            //Custom: "mt <16-bit hex> <optional 0-255 alpha, default 255>" for material set
            else if (line[0] == 'm' && line[1] == 't' && line[2] == ' ')
            {
                uint16_t color;
                uint8_t alpha = 255;
                sscanf(line, "mt %hx %hhu", &color, &alpha);
                currentMaterial = new Material(color, alpha);
            }
            // "usemtl" handler
            else if (line[0] == 'u' && line[1] == 's')
            {
                char materialName[256];
                if (sscanf(line, "usemtl %255s", materialName) != 1) break;
                if (materialLibrary == nullptr)
                {
                    printf("Warning: Material library not provided, ignoring material\n");
                }
                else 
                {
                    bool materialFound = false;
                    for (auto material : *materialLibrary)
                    {
                        if (strcmp(material->name, materialName) == 0)
                        {
                            currentMaterial = material;
                            materialFound = true;
                            break;
                        }
                    }
                    if (!materialFound)
                    {
                        printf("Warning: Material '%s' not found in the library\n", materialName);
                        printf("Available materials are:\n");
                        for (auto material : *materialLibrary)
                        {
                            printf("  %s\n", material->name);
                        }
                    }
                }
            }
            else if (line[0] == 'v' && line[1] == ' ')
            {
                float x, y, z;
                sscanf(line, "v %f %f %f", &x, &y, &z);
                tempVertices.push_back(x);
                tempVertices.push_back(y);
                tempVertices.push_back(z);
            }
            else if (line[0] == 'v' && line[1] == 't')
            {
                float u, v;
                sscanf(line, "vt %f %f", &u, &v);
                tempUVs.push_back(u);
                tempUVs.push_back(v);
            }
            else if (line[0] == 'v' && line[1] == 'n')
            {
                float x, y, z;
                sscanf(line, "vn %f %f %f", &x, &y, &z);
                tempNormals.push_back(x);
                tempNormals.push_back(y);
                tempNormals.push_back(z);
            }
            else if (line[0] == 'f')
            {
                int vertices[4][3] = {0}; // Array to store up to 4 vertices (v/t/n format)
                int numVertices = 0;
                const char* ptr = line + 1;
                
                // Skip whitespace
                while (*ptr == ' ') ptr++;
                
                // Parse up to 4 vertices
                for (int i = 0; i < 4 && *ptr && *ptr != '\n'; i++) {
                    if (sscanf(ptr, "%d/%d/%d", &vertices[i][0], &vertices[i][1], &vertices[i][2]) == 3) {
                        numVertices++;
                        // Skip to next vertex
                        while (*ptr && *ptr != ' ' && *ptr != '\n') ptr++;
                        while (*ptr == ' ') ptr++;
                    }
                }

                // Function to create and add a vertex
                auto createVertex = [&](int v, int t, int n) -> Object::Vertex {
                    Object::Vertex vert;
                    vert.position = {
                        (int32_t)(tempVertices[(v - 1) * 3] * SCALE),
                        (int32_t)(tempVertices[(v - 1) * 3 + 1] * SCALE),
                        (int32_t)(tempVertices[(v - 1) * 3 + 2] * SCALE)
                    };
                    vert.uv = {
                        (int32_t)(tempUVs[(t - 1) * 2] * FIXED_POINT_SCALE),
                        (int32_t)(tempUVs[(t - 1) * 2 + 1] * FIXED_POINT_SCALE)
                    };
                    // Lighting needs per-vertex normals in fixed-point with
                    // magnitude FIXED_POINT_SCALE. Without this assignment
                    // every imported model lit only by ambient (Lambert's
                    // dot product is always 0 for a zero-length normal),
                    // which is why ship faces never changed colour with
                    // orientation regardless of how the directional light
                    // was configured.
                    if (n > 0 && (size_t)(n * 3) <= tempNormals.size()) {
                        vert.normal = {
                            (int32_t)(tempNormals[(n - 1) * 3]     * FIXED_POINT_SCALE),
                            (int32_t)(tempNormals[(n - 1) * 3 + 1] * FIXED_POINT_SCALE),
                            (int32_t)(tempNormals[(n - 1) * 3 + 2] * FIXED_POINT_SCALE)
                        };
                    }
                    return vert;
                };

                if (numVertices >= 3) {
                    bool valid=true;
                    for(int i=0;i<numVertices;++i) {
                        const int v=vertices[i][0],t=vertices[i][1],n=vertices[i][2];
                        valid &= v>0 && size_t(v)<=tempVertices.size()/3 &&
                            t>0 && size_t(t)<=tempUVs.size()/2 && n>0 && size_t(n)<=tempNormals.size()/3;
                    }
                    if(valid) {
                        uint16_t face[4];
                        for(int i=0;i<numVertices;++i) {
                            const std::array<int,3> key{vertices[i][0],vertices[i][1],vertices[i][2]};
                            auto found=vertexLookup.find(key);
                            if(found!=vertexLookup.end()) face[i]=found->second;
                            else {
                                if(obj->vertices.size()>=65536) {valid=false;break;}
                                face[i]=uint16_t(obj->vertices.size());
                                obj->addVertex(createVertex(key[0],key[1],key[2]));
                                vertexLookup.emplace(key,face[i]);
                            }
                        }
                        if(valid) {
                            obj->addTriangle(face[0],face[1],face[2],currentMaterial);
                            if(numVertices==4) obj->addTriangle(face[0],face[2],face[3],currentMaterial);
                        }
                    }
                }
            }

            // Advance to next line
            while (*line && *line != '\n')
                line++;
            if (*line)
                line++;
        }

        obj->calculateBoundingBox();

        return obj;
    }

    [[maybe_unused]] static Object *LoadFromObjFile(char *filename, Material *defaultMaterial = nullptr, float scale = 1.0f) {
        FILE *file = fopen(filename, "rb");
        if (!file) {
            return nullptr;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *fileData = new char[fileSize + 1];
        fread(fileData, 1, fileSize, file);
        fileData[fileSize] = 0;

        fclose(file);

        Object *obj = LoadFromObjData(fileData, defaultMaterial, nullptr, scale);
        delete[] fileData;

        return obj;
    }
}

#pragma GCC diagnostic pop