struct VSInput {
    float3 inPosition;
    float3 inNormal;
};

struct FrameUniforms {
    float4x4 view;
    float4x4 projection;
}

ConstantBuffer<FrameUniforms> frame;

struct VSOutput {
    float4 pos : SV_Position;
    float3 normal;
};

struct DrawConstants {
    float4 baseColor;
};

[[vk::push_constant]]
DrawConstants draw;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;

    float4 viewPosition = mul(frame.view, float4(input.inPosition, 1.0));
    output.pos = mul(frame.projection, viewPosition);

    output.normal = input.inNormal;

    return output;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
   return draw.baseColor;
}