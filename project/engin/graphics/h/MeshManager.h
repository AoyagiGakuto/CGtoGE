#pragma once
#include "MakeAffine.h"
#include <vector>

enum MeshType {
    MeshType_Sphere,
    MeshType_Cube,
    MeshType_Plane,
    MeshType_Count
};

struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
};

struct MeshData {
    std::vector<VertexData> vertices;
    Transform transform;
};

class MeshManager {
public:
    MeshManager();
    MeshData& GetCurrentMesh();
    void SetCurrentMeshType(MeshType type);
    MeshType GetCurrentMeshType() const;
    std::vector<MeshData> meshes;

private:
    MeshType currentMeshType_;
    void InitMeshes();
};