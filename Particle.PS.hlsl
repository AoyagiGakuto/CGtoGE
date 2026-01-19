#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct MaterialData
{
    float4 color;
    int enableLighting;
    int shadingType; // 0: Lambert, 1: HalfLambert
    float shininess;
    float padding;
    float4x4 uvTransform;
};


cbuffer Material : register(b0)
{
    MaterialData gMaterial;
}

//ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};


//StructuredBuffer<DirectionalLight> gDirectionalLight : register(b1);

cbuffer DirectionalLightCB : register(b1)
{
    DirectionalLight gDirectionalLight;
}

struct Camera
{
    float3 worldPosition;
};

//ConstantBuffer<Camera> gCamera : register(b2);

cbuffer CameraCB : register(b2)
{
    Camera gCamera;
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

   // ライティングが有効な場合
    if (gMaterial.enableLighting != 0)
    {
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction); // 光源への向き
        float NdotL = dot(N, L);
        float lighting = max(NdotL, 0.0f); // Lambert

        if (gMaterial.shadingType == 1) // HalfLambert
        {
            lighting = NdotL * 0.5f + 0.5f;
        }

        // --- 鏡面反射 (Specular) ---
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        float3 reflectLight = reflect(gDirectionalLight.direction, N);
        float RdotE = dot(reflectLight, toEye);
        float specularPow = pow(saturate(RdotE), gMaterial.shininess);

        // 拡散反射
        float3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * lighting * gDirectionalLight.intensity;
        // 鏡面反射 (ハイライト)
        float3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float3(1.0f, 1.0f, 1.0f);

        // 合成
        output.color.rgb = diffuse + specular;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        // ライティング無効時
        output.color = gMaterial.color * textureColor;
    }
    
    // アルファテスト
    if (output.color.a == 0.0f)
    {
        discard;
    }
    
    return output;
}