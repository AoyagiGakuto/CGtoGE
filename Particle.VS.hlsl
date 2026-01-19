#include "Particle.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

StructuredBuffer<TransformationMatrix> gTransformationMatrix : register(t0);


struct VertexShaderInput
{
    float4 position : POSITION0; // èCê≥: SV_POSITION Å® POSITION0
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // èCê≥: NORMALO Å® NORMAL0
};

VertexShaderOutput main(VertexShaderInput input,uint instanceld : SV_InstanceID)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix[instanceld].WVP);
    output.worldPosition = mul(input.position, gTransformationMatrix[instanceld].World).xyz;
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix[instanceld].World));
    return output;
}