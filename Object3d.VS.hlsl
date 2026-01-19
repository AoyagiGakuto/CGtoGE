#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);


struct VertexShaderInput
{
    float4 position : POSITION0; // 修正: SV_POSITION → POSITION0
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 修正: NORMALO → NORMAL0
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0; // 追加
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    // ワールド行列を使ってワールド座標を計算 (.xyzだけ取り出す)
    output.worldPosition = mul(input.position, gTransformationMatrix.World).xyz; // 追加
    // 法線もワールド空間へ変換して正規化
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix.World));
    return output;
}