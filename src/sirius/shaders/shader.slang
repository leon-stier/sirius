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

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
   return draw.baseColor;
}