// --------------------------------------------------
// include
// --------------------------------------------------

#include "MakeAffine.h"
#include "ResourceObject.h"
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include <Windows.h>
#include <Xinput.h>
#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <format>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>
#include <wrl.h>

// --------------------------------------------------
// ライブラリのリンク
// --------------------------------------------------

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "xinput.lib")

// --------------------------------------------------
// using declarations
// --------------------------------------------------

using namespace std::numbers;
using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
        return true;
    }

    // メッセージに応じてゲーム固有の処理を行う
    switch (uMsg) {
        // ウィンドウが破棄された
    case WM_DESTROY:
        // OSに対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;

    default:
        // 標準のメッセージ処理を行う
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// --------------------------------------------------
// 関数、構造体定義
// --------------------------------------------------

// 文字列を出す
void Log(const std::string& message)
{
    OutputDebugStringA(message.c_str());
}

std::wstring ConvertString(const std::string& str)
{
    if (str.empty()) {
        return std::wstring();
    }
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) {
        return std::wstring();
    }
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}

std::string ConvertString(const std::wstring& str)
{
    if (str.empty()) {
        return std::string();
    }
    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) {
        return std::string();
    }
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}

DirectX ::ScratchImage LoadTexture(const std ::string filePath)
{
    DirectX::ScratchImage image {};
    std::wstring filePathW = ConvertString(filePath);
    HRESULT hr = DirectX ::LoadFromWICFile(filePathW.c_str(), DirectX ::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));
    DirectX ::ScratchImage mipImages {};
    hr = DirectX ::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX ::TEX_FILTER_SRGB, 8, mipImages);
    assert(SUCCEEDED(hr));
    return mipImages;
}

ComPtr<ID3D12Resource> CreateTextureResourse(ID3D12Device* device, const DirectX::TexMetadata& metadata)
{
    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
{
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

    for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
        const DirectX ::Image* img = mipImages.GetImage(mipLevel, 0, 0);
        HRESULT hr = texture->WriteToSubresource(
            UINT(mipLevel),
            nullptr,
            img->pixels,
            UINT(img->rowPitch),
            UINT(img->slicePitch));
        assert(SUCCEEDED(hr));
    }
}

ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes)
{
    // ヒーププロパティの設定
    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    // リソースの設定
    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // リソースの作成
    ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}

struct Vector4 {
    float x, y, z, w;
};

struct Vector2 {
    float x, y;
};

struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
};

struct Material {
    Vector4 color;
    int enableLighting;
    int shadingType; // 0: Lambert, 1: HalfLambert
    float padding[2];
    Matrix4x4 uvTransform;
};

struct MaterialData {
    std::string textureFilePath;
};

struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};

struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

struct DirectionalLight {
    Vector4 color;
    Vector3 direction;
    float intensity;
};

// 3Dベクトルの外積
inline Vector3 Cross(const Vector3& a, const Vector3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

enum BlendMode {
    // ブレンドなし
    kBlendModeNone,
    // 通常
    kBlendModeNormal,
    // 加算
    kBlendModeAdd,
    // 減算
    kBlendModeSubtract,
    // 乗算
    kBlendModeMultiply,
    // スクリーン
    kBlendModeScreen,
    // 利用しない
    kCountOfBlendMode,
};

// CPUディスクリプタハンドル取得関数
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

// GPUディスクリプタハンドル取得関数
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

struct D3D12ResourceLeakChecker {
    ~D3D12ResourceLeakChecker()
    {
        ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

IDxcBlob* CompileShader(
    // Comilerするファイルへのパス
    const std::wstring& filePath,
    // Compilerに使用するProfile
    const wchar_t* profile,
    // 初期化で生成したものを3つ
    ComPtr<IDxcUtils> dxcUtils, ComPtr<IDxcCompiler3> dxcCompiler, IDxcIncludeHandler* includeHandler)
{
    // 1.hlslファイルを読む
    Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));
    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // 2.Compileする
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E",
        L"main",
        L"-T",
        profile,
        L"-Zi",
        L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };

    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(&shaderResult));

    assert(SUCCEEDED(hr));

    // 3.警告・エラーが出てないか確認する
    IDxcBlobUtf8* shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(shaderError->GetStringPointer());
        assert(false); // エラーが出たので起動できない
    }

    // 4.Compile結果を受け取って返す
    IDxcBlob* shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));
    Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));
    // 読み込んだファイルのリソースを解放する
    shaderSource->Release();
    shaderResult->Release();
    return shaderBlob;
}

ComPtr<ID3D12Resource> CreateBufferResouse(ID3D12Device* device, size_t sizeInBytes)
{
    // 生成したShaderのリソースを解放する
    D3D12_HEAP_PROPERTIES uploadHeapProperties {};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    // 頂点リソースの設定
    D3D12_RESOURCE_DESC vertexResourceDesc {};
    // バッファリソース。テクスチャの場合はまた別の設定をする
    vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexResourceDesc.Width = sizeInBytes;
    // バッファの場合はこれらは1にする決まり
    vertexResourceDesc.Height = 1;
    vertexResourceDesc.DepthOrArraySize = 1;
    vertexResourceDesc.MipLevels = 1;
    vertexResourceDesc.SampleDesc.Count = 1;
    // バッファの場合はこれにする決まり
    vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // 実際に頂点リソースを作る
    ComPtr<ID3D12Resource> vertexResource = nullptr;
    HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexResource));
    assert(SUCCEEDED(hr));
    return vertexResource;
}

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    // ディスクリプタヒープの生成
    ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    // ディスクリプタヒープの生成
    descriptorHeapDesc.Type = heapType; // レンダーターゲットビュー用
    descriptorHeapDesc.NumDescriptors = numDescriptors; // ダブルバッファ用に2つ
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = device->CreateDescriptorHeap(
        &descriptorHeapDesc, // ディスクリプタヒープの設定
        IID_PPV_ARGS(&descriptorHeap) // ディスクリプタヒープのポインタ
    );

    // ディスクリプタヒープの生成に失敗したので起動できない
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}

ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(
    ID3D12Device* device, int32_t width, int32_t height)
{
    // 深度ステンシルテクスチャの設定
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Width = width; // 幅
    resourceDesc.Height = height; // 高さ
    resourceDesc.MipLevels = 1; // ミップレベル
    resourceDesc.DepthOrArraySize = 1; // 深度または配列サイズ
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 深度ステンシルフォーマット
    resourceDesc.SampleDesc.Count = 1; // サンプル数
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2Dテクスチャ
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // 深度ステンシルを許可

    // ヒーププロパティの設定
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE depthClearValue {};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& fileName)
{
    MaterialData materialData;
    std::string line;
    std::ifstream file(directoryPath + "/" + fileName);
    assert(file.is_open());
    while (std::getline(file, line)) {
        std::string identifer;
        std::istringstream s(line);
        s >> identifer;

        // identifierに応じた処理を行う
        if (identifer == "map_kd" || identifer == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }
    return materialData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& fileName)
{
    ModelData modelData;
    VertexData triangle[3];
    std::vector<Vector4> positions;
    std::vector<Vector2> texcoords;
    std::string line;
    std::ifstream fileStream(directoryPath + "/" + fileName);
    assert(fileStream.is_open());

    while (std::getline(fileStream, line)) {
        std::string identifer;
        std::istringstream s(line);
        s >> identifer;
        if (identifer == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.y *= -1.0f; // Z軸を反転
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifer == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifer == "f") {
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;
                std::istringstream v(vertexDefinition);
                std::string idxStr;
                uint32_t posIdx = 0, texIdx = 0;
                // 位置
                if (std::getline(v, idxStr, '/'))
                    posIdx = std::stoi(idxStr);
                // UV
                if (std::getline(v, idxStr, '/'))
                    texIdx = std::stoi(idxStr);
                Vector4 position = positions[posIdx - 1];
                Vector2 texcoord = texcoords[texIdx - 1];
                VertexData vertex = { position, texcoord };
                // modelData.vertices.push_back(vertex);
                triangle[faceVertex] = { position, texcoord };
            }
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        } else if (identifer == "mtllib") {
            std::string materialFileName;
            s >> materialFileName;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFileName);
        }
    }
    return modelData;
}

// --------------------------------------------------
// メイン関数
// --------------------------------------------------

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    D3D12ResourceLeakChecker leakCheck;

    // DXGIファクトリーの生成
    ComPtr<IDXGIFactory7> dxgiFactory;

    ComPtr<ID3D12Device> device;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WNDCLASS wc = {};

    // ウィンドクラスプロシージャ
    wc.lpfnWndProc = WindowProc;

    // ウィンドクラス名
    wc.lpszClassName = L"CG2WindowClass";

    // インスタンスハンドル
    wc.hInstance = GetModuleHandleW(nullptr);

    // カーソル
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    // ウィンドクラスの登録
    RegisterClassW(&wc);

    // クライアント領域のサイズ
    const int32_t kClientWidth = 1280;
    const int32_t kClientHeight = 720;

    // ウィンドウサイズを表す構造体にクライアント領域を入れる
    RECT wrc = { 0, 0, kClientWidth, kClientHeight };

    // クライアント領域をもとに実際のサイズにwrcを変更
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    // ウィンドウの生成
    HWND hwnd = CreateWindow(
        wc.lpszClassName, // 利用するクラス名
        L"CG2", // タイトルバーの文字
        WS_OVERLAPPEDWINDOW, // よく見るウィンドウスタイル
        CW_USEDEFAULT, // 表示x座標(Windowsに任せる)
        CW_USEDEFAULT, // 表示y座標(WindowsOSに任せる)
        wrc.right - wrc.left, // ウィンドウ横幅
        wrc.bottom - wrc.top, // ウィンドウ縦幅
        nullptr, // 親ウィンドウハンドル
        nullptr, // メニューハンドル
        wc.hInstance, // インスタンスハンドル
        nullptr // オプション
    );

    // ウィンドウを表示する
    ShowWindow(hwnd, SW_SHOW);

    MSG msg {};

#ifdef _DEBUG
    ComPtr<ID3D12Debug1> debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        // デバッグレイヤーを有効にする
        debugController->EnableDebugLayer();
    } else {
        // さらにGPU側でもチェックを行うようにする
        debugController->SetEnableGPUBasedValidation(TRUE);
    }

#endif // DEBUG

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

    assert(SUCCEEDED(hr));

    // 使用するアダプタ用の変数
    ComPtr<IDXGIAdapter4> useAdapter = nullptr;

    // いい順にアダプタを頼む
    for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        // アダプターの情報を取得する
        DXGI_ADAPTER_DESC3 adapterDesc {};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));

        // ソフトウェアアダプタでなければ採用!
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            Log(ConvertString(std::format(L"USE Adapter:{}\n", adapterDesc.Description)));
            break;
        }
        useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
    }

    // 適切なアダプタが見つからなかったので起動できない
    assert(useAdapter != nullptr);

    // 機能レベルとログ出力用の文字列
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };

    const char* featureLevelStrings[] = {
        "12.2",
        "12.1",
        "12.0",
    };

    // 高い順に生成できるか試していく
    for (size_t i = 0; i < _countof(featureLevels); i++) {
        // 採用したアダプターでデバイスの生成
        hr = D3D12CreateDevice(
            useAdapter.Get(), // アダプタ
            featureLevels[i], // 機能レベル
            IID_PPV_ARGS(&device) // デバイス
        );

        // 指定した機能レベルでログ出力を行ってループを抜ける
        if (SUCCEEDED(hr)) {
            Log((std::format("Feature Level: {}\n", featureLevelStrings[i])));
            break;
        }
    }

    // デバイスの生成に失敗したので起動できない
    assert(device != nullptr);

    // コマンドキューを生成する
    ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};

    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

    // コマンドキューの生成に失敗したので起動できない
    assert(SUCCEEDED(hr));

    // コマンドアロケータを生成する
    ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;

    // コマンドアロケータの生成
    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, // コマンドリストの種類
        IID_PPV_ARGS(&commandAllocator) // コマンドアロケータのポインタ
    );

    // コマンドアロケータの生成に失敗したので起動できない
    assert(SUCCEEDED(hr));

    // コマンドリストを生成する
    ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;

    // コマンドリストの生成
    hr = device->CreateCommandList(
        0, // コマンドリストのフラグ
        D3D12_COMMAND_LIST_TYPE_DIRECT, // コマンドリストの種類
        commandAllocator.Get(), // コマンドアロケータ
        nullptr, // パイプラインステートオブジェクト
        IID_PPV_ARGS(&commandList) // コマンドリストのポインタ
    );

    // コマンドリストの生成に失敗したので起動できない
    assert(SUCCEEDED(hr));

    // スワップチェーンを生成する
    ComPtr<IDXGISwapChain4> swapChain = nullptr;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};

    // スワップチェーンの設定
    swapChainDesc.Width = kClientWidth; // 画面の幅
    swapChainDesc.Height = kClientHeight; // 画面の高さ
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
    swapChainDesc.SampleDesc.Count = 1; // マルチサンプリングしない
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
    swapChainDesc.BufferCount = 2; // ダブルバッファ
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // スワップ効果
    // コマンドキュー、ウィンドウハンドル、スワップチェーンの設定
    hr = dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr,
        reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));

    assert(SUCCEEDED(hr));

    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(
        device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(
        device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    // スワップチェーンからリソースを引っ張ってくる
    ComPtr<ID3D12Resource> swapChainResoures[2] = { nullptr };

    // スワップチェーンのリソースを取得する
    hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResoures[0]));

    // スワップチェーンのリソースの取得に失敗したので起動できない
    assert(SUCCEEDED(hr));

    // スワップチェーンのリソースを取得する
    hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResoures[1]));

    // スワップチェーンのリソースの取得に失敗したので起動できない
    assert(SUCCEEDED(hr));

    // RTVの設定
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};

    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 色の形式
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // テクスチャ2D

    // ディスクリプタの先頭を取得する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    // RTVを2つ分確保する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

    // まず1つ目のスワップチェーンのリソースにRTVを設定する
    rtvHandles[0] = rtvStartHandle;
    device->CreateRenderTargetView(swapChainResoures[0].Get(), &rtvDesc, rtvHandles[0]);

    // 2つ目のスワップチェーンのリソースにRTVを設定する
    rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 2つ目を作る
    device->CreateRenderTargetView(swapChainResoures[1].Get(), &rtvDesc, rtvHandles[1]);

    // 初期値0でFrenceを作る
    ComPtr<ID3D12Fence> fence = nullptr;
    uint64_t fenceValue = 0;
    hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    assert(SUCCEEDED(hr));

    // FenceのSignalを持つためのイベントを作成する
    HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent != nullptr);

    // dxcCompilerを初期化
    ComPtr<IDxcUtils> dxcUtils = nullptr;
    ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));

    // 現時点でincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler* includeHandler = nullptr;
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature {};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_DESCRIPTOR_RANGE descriptorRanges[1] = {};
    descriptorRanges[0].BaseShaderRegister = 0;
    descriptorRanges[0].NumDescriptors = 1;
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
    descriptorRangeForInstancing[0].BaseShaderRegister = 0; // 0から始まる
    descriptorRangeForInstancing[0].NumDescriptors = 1; // 数は1つ
    descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
    descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRanges;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRanges);
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    // レジスタ番号1を使う
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // シリアライズしてバイナリにする
    ID3DBlob* signatureBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature,
        D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
        assert(false);
    }

    // バイナリを元に生成
    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    hr = device->CreateRootSignature(0,
        signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));

    assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0]
        .SemanticIndex
        = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc {};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    D3D12_BLEND_DESC blendDesc {};

    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // ついかしたところ
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    int blendMode = kBlendModeNormal;

    // ブレンドモードによって設定を変える
    switch (blendMode) {
    case 0: // なし
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        break;
    case 1: // ふつう
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;
    case 2: // 加算
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;
    case 3: // 減算
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        break;
    case 4: // 乗算
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;
    case 5: // スクリーン
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        break;
    }

    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    D3D12_RASTERIZER_DESC rasterizerDesc {};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // Shaderをコンパイルする
    // こっちかも
    IDxcBlob* vertexShaderBlob = CompileShader(L"Particle.VS.hlsl",
        L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
    assert(vertexShaderBlob != nullptr);

    IDxcBlob* pixelShaderBlob = CompileShader(L"Particle.PS.hlsl",
        L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
    assert(pixelShaderBlob != nullptr);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc {};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignature
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize() }; // VertexShader
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
        pixelShaderBlob->GetBufferSize() }; // PixelShader
    graphicsPipelineStateDesc.BlendState = blendDesc; // BlendState
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc; // RasterizerSt
    // 書き込むRTVの情報
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // 利用するトポロジ(形状)のタイプ。三角形
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // どのように画面に色を打ち込むかの設定
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // DepthStencilを設定
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // 実際に生成
    ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
        IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12Resource> wvpResource = CreateBufferResouse(device.Get(), sizeof(TransformationMatrix));
    TransformationMatrix* wvpData = nullptr;
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

    ComPtr<ID3D12Resource> materialResource = CreateBufferResouse(device.Get(), sizeof(Vector4) * 3);

    Material* materialData = nullptr;
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->uvTransform = MakeIdentity4x4();

    // modelDataを読み込む
    // ModelData modelData = LoadObjFile("Resources/fence", "fence.obj");
    ModelData modelData;
    modelData.vertices.push_back({ .position = { 1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左上
    modelData.vertices.push_back({ .position = { -1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
    modelData.vertices.push_back({ .position = { 1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
    modelData.vertices.push_back({ .position = { 1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
    modelData.vertices.push_back({ .position = { -1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
    modelData.vertices.push_back({ .position = { -1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右下

    // --- 頂点バッファ生成前に定義 ---
    const uint32_t kSubdivision = 32; // 分割数（大きいほど滑らか）
    const uint32_t kSphereVertexCount = kSubdivision * kSubdivision * 6;

    // 頂点バッファ用リソースを作成
    // ComPtr<ID3D12Resource> vertexResource = CreateBufferResouse(device.Get(), sizeof(VertexData) * kSphereVertexCount);
    ComPtr<ID3D12Resource> vertexResource = CreateBufferResouse(device.Get(), sizeof(VertexData) * modelData.vertices.size());

    // 頂点データを書き込む
    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 頂点コピー
    memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
    // 面法線を付与（3頂点ごと）
    for (size_t i = 0; i + 2 < modelData.vertices.size(); i += 3) {
        const Vector4& p0 = vertexData[i + 0].position;
        const Vector4& p1 = vertexData[i + 1].position;
        const Vector4& p2 = vertexData[i + 2].position;
        Vector3 e1 { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        Vector3 e2 { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
        Vector3 n = Cross(e1, e2);
        float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-6f) {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        }
        vertexData[i + 0].normal = vertexData[i + 1].normal = vertexData[i + 2].normal = n;
    }

    vertexResource->Unmap(0, nullptr);

    /*
    // 球体メッシュ生成
    const float kRadius = 1.0f;
    const float kPi = std::numbers::pi_v<float>;
    const float kTwoPi = kPi * 2.0f;
    uint32_t vertexIdx = 0;
    for (uint32_t lat = 0; lat < kSubdivision; ++lat) {
        float lat0 = kPi * (float(lat) / kSubdivision - 0.5f); // -π/2 ～ +π/2
        float lat1 = kPi * (float(lat + 1) / kSubdivision - 0.5f);
        for (uint32_t lon = 0; lon < kSubdivision; ++lon) {
            float lon0 = kTwoPi * float(lon) / kSubdivision;
            float lon1 = kTwoPi * float(lon + 1) / kSubdivision;

            // 4点の球面座標
            Vector4 p00 = { kRadius * cos(lat0) * cos(lon0), kRadius * sin(lat0), kRadius * cos(lat0) * sin(lon0), 1.0f };
            Vector4 p01 = { kRadius * cos(lat0) * cos(lon1), kRadius * sin(lat0), kRadius * cos(lat0) * sin(lon1), 1.0f };
            Vector4 p10 = { kRadius * cos(lat1) * cos(lon0), kRadius * sin(lat1), kRadius * cos(lat1) * sin(lon0), 1.0f };
            Vector4 p11 = { kRadius * cos(lat1) * cos(lon1), kRadius * sin(lat1), kRadius * cos(lat1) * sin(lon1), 1.0f };

            // テクスチャ座標
            Vector2 uv00 = { float(lon) / kSubdivision, 1.0f - float(lat) / kSubdivision };
            Vector2 uv01 = { float(lon + 1) / kSubdivision, 1.0f - float(lat) / kSubdivision };
            Vector2 uv10 = { float(lon) / kSubdivision, 1.0f - float(lat + 1) / kSubdivision };
            Vector2 uv11 = { float(lon + 1) / kSubdivision, 1.0f - float(lat + 1) / kSubdivision };

            // 2三角形
            vertexData[vertexIdx++] = { p00, uv00 };
            vertexData[vertexIdx++] = { p10, uv10 };
            vertexData[vertexIdx++] = { p11, uv11 };

            vertexData[vertexIdx++] = { p00, uv00 };
            vertexData[vertexIdx++] = { p11, uv11 };
            vertexData[vertexIdx++] = { p01, uv01 };
        }
    }

    for (uint32_t i = 0; i < vertexIdx; ++i) {
        vertexData[i].normal.x = vertexData[i].position.x;
        vertexData[i].normal.y = vertexData[i].position.y;
        vertexData[i].normal.z = vertexData[i].position.z;
    }

    */

    ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
    DirectionalLight* directionalLightData = nullptr;
    directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

    // デフォルト値はとりあえず以下のようにしておく
    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    const uint32_t kNumInstance = 10; // いたポリ枚数
    // Instancing用のTransformationMatrixリソースを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = CreateBufferResouse(device.Get(), sizeof(TransformationMatrix) * kNumInstance);
    // 書き込むためのアドレスを取得
    TransformationMatrix* instancingData = nullptr;
    instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));
    // 単位行列を書き込んでおく
    for (uint32_t index = 0; index < kNumInstance; ++index) {
        instancingData[index].WVP = MakeIdentity4x4();
        instancingData[index].World = MakeIdentity4x4();
    }

    // SRVディスクリプタサイズ
    UINT descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc = {};
    instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instancingSrvDesc.Buffer.FirstElement = 0;
    instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    instancingSrvDesc.Buffer.NumElements = kNumInstance;
    instancingSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);
    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
    device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

    // Sprite用のマテリアルリソースを作る
    ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device.Get(), sizeof(Material));

    vertexResource->Unmap(0, nullptr);

    // Lightingを有効にする
    Material* materialDataSprite = nullptr;
    materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

    materialDataSprite->enableLighting = false;

    // 頂点バッファビュー
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    // vertexBufferView.SizeInBytes = sizeof(VertexData) * kSphereVertexCount;
    vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    // ビューポート
    D3D12_VIEWPORT viewport {};
    // クライアント領域のサイズと一緒にして画面全体に表示
    viewport.Width = kClientWidth;
    viewport.Height = kClientHeight;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    // シザー矩形
    D3D12_RECT scissorRect {};
    // 基本的にビューポートと同じ矩形が構成されるようにする
    scissorRect.left = 0;
    scissorRect.right = kClientWidth;
    scissorRect.top = 0;
    scissorRect.bottom = kClientHeight;

    Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);

    Matrix4x4* transformationMatrixData = nullptr;
    ComPtr<ID3D12Resource> transformationMatrixResource = CreateBufferResouse(device.Get(), sizeof(Matrix4x4));
    transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));

    ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight);
    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(
        device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(
        depthStencilResource.Get(),
        &dsvDesc,
        dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // Sprite用の瓦点リソースを作る
    ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResouse(device.Get(), sizeof(VertexData) * 6);

    // 瓦点バッファビューを作成する
    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite {};

    // リソースの先品のアドレスから使ラ
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

    // 使用するリソースのサイズは頂点のつ分のサイズ
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;

    // 1頂点あたりのリイズ
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    VertexData* vertexDataSprite = nullptr;
    vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
    vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
    vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
    vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
    vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[2].texcoord = { 1.0f, 1.0f };

    vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
    vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
    vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[5].texcoord = { 1.0f, 1.0f };

    ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResouse(device.Get(), sizeof(Matrix4x4));
    Matrix4x4* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    *transformationMatrixDataSprite = MakeIdentity4x4();

    Transform transformSprite {
        {
            1.0f,
            1.0f,
            1.0f,
        },
        {
            0.0f,
            0.0f,
            0.0f,
        },
        {
            0.0f,
            0.0f,
            0.0f,
        }
    };

    materialDataSprite->uvTransform = MakeIdentity4x4();

    Transform uvTransformSprite {
        {
            1.0f,
            1.0f,
            1.0f,
        },
        {
            0.0f,
            0.0f,
            0.0f,
        },
        {
            0.0f,
            0.0f,
            0.0f,
        }
    };

    Transform transforms[kNumInstance];
    for (uint32_t index = 0; index < kNumInstance; ++index) {
        transforms[index].scale = { 1.0f, 1.0f, 1.0f };
        transforms[index].rotate = { 0.0f, 0.0f, 0.0f };
        transforms[index].translate = { index * 0.1f, index * 0.1f, index * 0.0f };
    }

    std::vector<std::string> textureFiles = {
        "Resources/fence.png",
        "Resources/uvChecker.png",
        "Resources/monsterBall.png",
    };
    std::vector<ComPtr<ID3D12Resource>> textureResources;
    std::vector<DirectX::ScratchImage> mipImagesList;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> textureSrvHandlesCPU;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> textureSrvHandlesGPU;

    UINT srvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    // 先頭はImGui用に1つ使われているので+1
    srvHandleCPU.ptr += srvIncrement;
    srvHandleGPU.ptr += srvIncrement;

    for (size_t i = 0; i < textureFiles.size(); ++i) {
        mipImagesList.push_back(LoadTexture(textureFiles[i]));
        const DirectX::TexMetadata& metadata = mipImagesList.back().GetMetadata();
        ComPtr<ID3D12Resource> texRes = CreateTextureResourse(device.Get(), metadata);
        UploadTextureData(texRes.Get(), mipImagesList.back());
        textureResources.push_back(texRes);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = metadata.format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

        device->CreateShaderResourceView(texRes.Get(), &srvDesc, srvHandleCPU);

        textureSrvHandlesCPU.push_back(srvHandleCPU);
        textureSrvHandlesGPU.push_back(srvHandleGPU);

        srvHandleCPU.ptr += srvIncrement;
        srvHandleGPU.ptr += srvIncrement;
    }

    // ImGuiの初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device.Get(),
        swapChainDesc.BufferCount,
        rtvDesc.Format,
        srvDescriptorHeap.Get(),
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 0.3f, 0.0f, 1.0f); // オレンジ

    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); // 黄色
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); // 濃いオレンジ

    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.8f, 0.5f, 0.3f, 1.0f); // 薄いオレンジ
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.3f, 0.2f, 1.0f); // 赤

    DirectX::ScratchImage mipImages = LoadTexture("Resources/uvChecker.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    ComPtr<ID3D12Resource> textureResouce = CreateTextureResourse(device.Get(), metadata);
    UploadTextureData(textureResouce.Get(), mipImages);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvStartHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvStartHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    textureSrvStartHandleCPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    textureSrvStartHandleGPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CreateShaderResourceView(textureResouce.Get(), &srvDesc, textureSrvStartHandleCPU);

    static int kyu = 0;
    static int sphereTextureIndex = 0;

    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    materialDataSprite->color = { 1.0f, 1.0f, 1.0f };
    materialDataSprite->enableLighting = true;

    // 透明度
    materialDataSprite->color.w = 1.0f;

    // --------------------------------------------------
    // メインループ
    // --------------------------------------------------

    // ウィンドウの×ボタンが押されるまでループ
    while (msg.message != WM_QUIT) {
        // windowsにメッセージが来てたら最優先で処理させる
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // ゲームの処理
            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex(); // バックバッファのインデックス

            // 描画先のRTVを取得する
            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);

            // TransitionBarrierの設定
            D3D12_RESOURCE_BARRIER barrier {};

            // 今回のバリアはTransition
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

            // Noneにする
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

            // バリアを張る対象のリソース。現在のバックバッファに対して行う
            barrier.Transition.pResource = swapChainResoures[backBufferIndex].Get();

            // 漂移前のResourceState
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;

            // 漂移後のResourceState
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

            // TransitionBarrierを張る
            commandList->ResourceBarrier(1, &barrier);

            // コントローラー
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(XINPUT_STATE));
            DWORD dwResult = XInputGetState(0, &state); // 0は1Pコントローラー

            if (dwResult == ERROR_SUCCESS) {
                // 十字キー
                io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0);
                io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
                io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
                io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);

                // 決定（A）、戻る（B）
                io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0); // 決定
                io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0); // 戻る

                // 左スティックで数値変更（例：左右で数値増減）
                float lx = state.Gamepad.sThumbLX / 32767.0f;
                float ly = state.Gamepad.sThumbLY / 32767.0f;
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, lx < -0.3f, lx); // 左
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, lx > 0.3f, lx); // 右
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ly > 0.3f, ly); // 上
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ly < -0.3f, ly); // 下
            }

            static int sphereShadingType = 0; // 0: Lambert, 1: HalfLambert
            static bool sphereEnableLighting = false;

            // ブレンドモード
            static int prevBlendMode = -1;

            if (blendMode != prevBlendMode) {
                // BlendDesc
                blendDesc.RenderTarget[0].BlendEnable = TRUE;
                switch (blendMode) {
                case 0:
                    blendDesc.RenderTarget[0].BlendEnable = FALSE;
                    break;
                case 1:
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    break;
                case 2:
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    break;
                case 3:
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
                    break;
                case 4:
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    break;
                case 5:
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    break;
                }

                blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

                graphicsPipelineStateDesc.BlendState = blendDesc;
                device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
                prevBlendMode = blendMode;
            }

            ImGui::ShowDemoWindow();

            ImGui::Begin("Main Control");

            /*
            // --- Sprite ---
            ImGui::Text("Sprite");
            ImGui::Separator();
            ImGui::DragFloat3("Sprite Position", &transformSprite.translate.x);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the position of the sprite (X, Y, Z)");

            ImGui::DragFloat3("Sprite Rotation", &transformSprite.rotate.x);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the rotation of the sprite (angles for X, Y, Z axes)");

            ImGui::DragFloat3("Sprite Scale", &transformSprite.scale.x);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the scale of the sprite (multipliers for X, Y, Z axes)");

            ImGui::DragFloat2("Sprite UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the translation (offset) of the sprite's UV coordinates");

            ImGui::DragFloat2("Sprite UV Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the scale of the sprite's UV coordinates");

            ImGui::SliderAngle("Sprite UVRotate", &uvTransformSprite.rotate.z);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rotate the sprite's UV coordinates");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            */

            // --- Obj  ---
            ImGui::Text("Obj");
            ImGui::Separator();
            ImGui::DragFloat3("Obj Position", &transform.translate.x, 0.1f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the position of the Obj (X, Y, Z)");

            ImGui::DragFloat3("Obj Rotation", &transform.rotate.x, 0.01f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the rotation of the Obj (angles for X, Y, Z axes)");

            ImGui::DragFloat3("Obj Scale", &transform.scale.x, 0.01f, 0.1f, 10.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the scale of the Obj (multipliers for X, Y, Z axes)");

            ImGui::ColorEdit3("Obj Color", &materialDataSprite->color.x);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the Obj's color (RGB)");

            // 透明度
            ImGui::SliderFloat("Obj Alpha", &materialDataSprite->color.w, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the Obj transparency (alpha)");

            ImGui::Combo("Obj Texture", &sphereTextureIndex, "texture1\0texture2\0texture3\0");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Select the texture image for the Obj");

            ImGui::Checkbox("Enable Lighting", &sphereEnableLighting);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Enable or disable lighting effects on the Obj");

            const char* shadingTypes[] = { "Lambert", "HalfLambert" };
            ImGui::Combo("Obj Shading", &sphereShadingType, shadingTypes, IM_ARRAYSIZE(shadingTypes));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Select the shading method for the Obj");

            // ぶれんどもーど
            const char* blendModeNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
            ImGui::Combo("Blend Mode", &blendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the blend mode for the Obj");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- Light ---
            ImGui::Text("Light");
            ImGui::Separator();
            ImGui::DragFloat3("Light Direction", &directionalLightData->direction.x, 0.01f, -1.0f, 1.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the direction of the light (X, Y, Z)");

            // 明るさ
            ImGui::SliderFloat("Light Intensity", &directionalLightData->intensity, 0.0f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Change the intensity (brightness) of the light");

            ImGui::End();

            materialDataSprite->enableLighting = sphereEnableLighting;
            materialDataSprite->shadingType = sphereShadingType;

            // 光源方向の正規化
            {
                float& x = directionalLightData->direction.x;
                float& y = directionalLightData->direction.y;
                float& z = directionalLightData->direction.z;
                float len = sqrtf(x * x + y * y + z * z);
                if (len > 0.0001f) {
                    x /= len;
                    y /= len;
                    z /= len;
                }
            }

            ImGui::Render();

            // 指定した色で画面全体をクリアする
            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 青っぽい色、RGBAの順

            commandList->ClearRenderTargetView(
                rtvHandles[backBufferIndex], // 描画先のRTV
                clearColor, // クリアする色
                0, // フラグ
                nullptr // 深度ステンシルビューのハンドル
            );

            ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
            commandList->SetDescriptorHeaps(1, descriptorHeaps);

            // 今回はRenderTargetからPresentにする
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

            transform.rotate.y += 0.00f;

            // カメラの位置をz=-10.0fに設定
            Transform cameraTransform {
                {
                    1.0f,
                    1.0f,
                    1.0f,
                },
                {
                    0.0f,
                    0.0f,
                    0.0f,
                },
                {
                    0.0f,
                    0.0f,
                    -10.0f,
                }
            };

            Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
            Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
            Matrix4x4 viewMatrix = Inverse(cameraMatrix);
            Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
            Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
            wvpData->WVP = worldViewProjectionMatrix;
            wvpData->World = worldMatrix;

            Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
            uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
            uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
            materialData->uvTransform = uvTransformMatrix;

            Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
            Matrix4x4 viewMatrixSprite = MakeIdentity4x4();

            Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(
                0.0f, 0.0f,
                float(kClientWidth), float(kClientHeight),
                0.0f, 100.0f);

            for (uint32_t index = 0; index < kNumInstance; ++index) {
                Matrix4x4 worldMatrix = MakeAffineMatrix(transforms[index].scale, transforms[index].rotate, transforms[index].translate);
                Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, worldViewProjectionMatrix);
                instancingData[index].WVP = worldViewProjectionMatrix;
                instancingData[index].World = worldMatrix;
            }

            Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
            *transformationMatrixDataSprite = worldViewProjectionMatrixSprite;

            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
            commandList->ClearDepthStencilView(
                dsvHandle,
                D3D12_CLEAR_FLAG_DEPTH,
                1.0f,
                0,
                0,
                nullptr);

            // TransitionBarrierを張る
            commandList->SetGraphicsRootSignature(rootSignature.Get());
            commandList->SetPipelineState(graphicsPipelineState.Get());
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
            // commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);
            commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandlesGPU[sphereTextureIndex]);
            commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            // commandList->DrawInstanced(kSphereVertexCount, 1, 0, 0);
            commandList->DrawInstanced(UINT(modelData.vertices.size()), kNumInstance, 0, 0);

            // スプライト描画
            commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
            commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
            //commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);
            commandList->DrawInstanced(6, kNumInstance, 0, 0);

            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

            hr = commandList->Close();

            // コマンドリストの生成に失敗したので起動できない
            assert(SUCCEEDED(hr));

            // GPUのコマンドリスト実行を行わせる
            ID3D12CommandList* commandLists[] = { commandList.Get() };

            commandQueue->ExecuteCommandLists(1, commandLists);

            // GPUとOSに画面の交換を行うように通知する
            swapChain->Present(1, 0);

            // Fenceの値を更新
            fenceValue++;

            // GPUがここまでたどり着いたときに、Fenceの値を設定した値に代入するようにSignalを送る
            commandQueue->Signal(fence.Get(), fenceValue);

            // Fenceの値が指定したSignal値にたどり着いているか確認する
            // GetComplatedValueの初期値はFenceに渡した初期値
            if (fence->GetCompletedValue() < fenceValue) {
                // 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
                fence->SetEventOnCompletion(fenceValue, fenceEvent);
                // イベントを待つ
                WaitForSingleObject(fenceEvent, INFINITE);
            }

            // 次のフレーム用のコマンドリストを準備
            hr = commandAllocator->Reset();
            assert(SUCCEEDED(hr));
            hr = commandList->Reset(commandAllocator.Get(), nullptr);
            assert(SUCCEEDED(hr));
        }
    }

    // 出力ウィンドウへの文字入力
    OutputDebugStringA("Hello, DirectX!\n");

    Log(ConvertString(std::format(L"WSTRING{}\n", L"abc")));

    Log("Complete create D3D12Device!!!\n"); // 初期化完了のログを出す

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        // デバッグレイヤーのメッセージを全て出力する
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        // エラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        // 警告時に泊まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        // 抑制するメッセージのID
        D3D12_MESSAGE_ID denyIds[] = {
            // Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
        };

        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

        D3D12_INFO_QUEUE_FILTER filter {};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;

        // 指定したメッセージの表示を無力化する
        infoQueue->PushStorageFilter(&filter);
    }
#endif

    // --------------------------------------------------
    // 終了
    // --------------------------------------------------

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CoUninitialize();

    CloseWindow(hwnd);

    // --------------------------------------------------
    // デバッグ用リソースリークチェック
    // --------------------------------------------------

    // リソースリークチェック
    IDXGIDebug1* debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
        /*
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        debug->Release();
        */
    }

    return 0;
}