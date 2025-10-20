#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    int shadingType; // 0: Lambert, 1: HalfLambert
    float2 padding;
    float4x4 uvTransform;
};

StructuredBuffer<Material> gMaterial : register(t1);

/*
struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};j
*/

//StructuredBuffer<DirectionalLight> gDirectionalLight : register(t2);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    output.color = gMaterial.color * textureColor;
    
    // output.colorのα値が0のときピクセルを棄却
    if (output.color.a == 0.0f)
    {
        discard;
    }
    
    return output;
}