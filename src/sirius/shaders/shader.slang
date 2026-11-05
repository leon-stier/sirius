

struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float2 inUv;
};

struct FrameUniforms {
    float4x4 view;
    float4x4 projection;
}

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> frame;

[[vk::binding(0, 1)]]
Texture2D<float4> baseColorTexture;

[[vk::binding(1, 1)]]
SamplerState baseColorSampler;

struct VSOutput {
    float4 pos : SV_Position;
    float3 normal;
    float2 uv;
};

struct DrawConstants {
    float4x4 model;
    float4 baseColor;
};

[[vk::push_constant]]
DrawConstants draw;



[shader("vertex")]
VSOutput vertMain(VSInput input) {

    VSOutput output;

    float4 modelPosition = mul(draw.model, float4(input.inPosition, 1.0));
    float4 viewPosition = mul(frame.view, modelPosition);
    output.pos = mul(frame.projection, viewPosition);

    output.normal = input.inNormal;
    output.uv = input.inUv;

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    return baseColorTexture.Sample(baseColorSampler, vertIn.uv);// * draw.baseColor;
}