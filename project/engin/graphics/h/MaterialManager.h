#pragma once
#include "MakeAffine.h"
#include <vector>

struct Material {
    Vector4 color;
    int enableLighting;
    float padding[3];
    Matrix4x4 uvTransform;
};

class MaterialManager {
public:
    MaterialManager();
    Material& GetCurrentMaterial();
    void SetCurrentMaterialIndex(size_t index);
    size_t GetCurrentMaterialIndex() const;
    std::vector<Material> materials;

private:
    size_t currentMaterialIndex_;
    void InitMaterials();
};