#include "LightingMode.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

extern MeshManager meshManager;
extern MaterialManager materialManager;
extern int lightingMode;

void ShowControls()
{
    ImGui::Combo("Mesh", (int*)&meshManager.currentMeshType_, "Sphere\0Cube\0Plane\0");
    for (int i = 0; i < MeshType_Count; ++i) {
        ImGui::PushID(i);
        ImGui::Text("Mesh %d Transform", i);
        ImGui::DragFloat3("Scale", &meshManager.meshes[i].transform.scale.x, 0.01f);
        ImGui::DragFloat3("Rotation", &meshManager.meshes[i].transform.rotate.x, 0.01f);
        ImGui::DragFloat3("Translate", &meshManager.meshes[i].transform.translate.x, 0.01f);
        ImGui::PopID();
    }
    ImGui::Combo("Material", (int*)&materialManager.currentMaterialIndex_, "Red\0Green\0Blue\0White\0");
    ImGui::Combo("Lighting Mode", &lightingMode, "None\0Lambert\0Half Lambert\0");
}