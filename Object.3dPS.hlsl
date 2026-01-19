#include "Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    float shininess;
    int shadingType; // 0: Lambert, 1: HalfLambert
    float2 padding;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct Camera
{
    float3 worldPosition;
};

ConstantBuffer<Camera> gCamera : register(b2);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0; // VSに合わせて追加
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // ライティング計算
    if (gMaterial.enableLighting != 0)
    {
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction); // ライトへの方向
        float NdotL = dot(N, L);
        float lighting = max(NdotL, 0.0f); // Lambert

        if (gMaterial.shadingType == 1) // HalfLambertの場合
        {
            lighting = NdotL * 0.5f + 0.5f;
        }

        // --- 鏡面反射 の計算 ---
        // 視線ベクトル (カメラ位置 - ピクセル位置)
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

        // 反射ベクトル
        float3 reflectLight = reflect(gDirectionalLight.direction, N);

        // 鏡面反射の強さを計算
        float RdotE = dot(reflectLight, toEye);
        
        // ハイライト
        float specularPow = pow(saturate(RdotE), gMaterial.shininess);

        // 拡散反射色
        float3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * lighting * gDirectionalLight.intensity;

        // 鏡面反射色
        float3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float3(1.0f, 1.0f, 1.0f);

        // 最終合成: 拡散反射 + 鏡面反射
        output.color.rgb = diffuse + specular;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }

    // アルファテスト等は既存のまま
    if (output.color.a == 0.0f)
        discard;
    if (textureColor.a == 0.0f)
        discard;

    return output;
}