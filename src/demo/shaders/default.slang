#language slang 2026

module default;

struct Uniforms {
  float4x4 model;
  float4x4 view;
  float4x4 projection;
};
ConstantBuffer<Uniforms> ubo;

struct VertexInput {
  float3 position;
  float3 color;
};

struct VertexOutput {
  float4 position : SV_Position;
  float3 color;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
  VertexOutput output;
  output.position = mul(ubo.projection, mul(ubo.view, mul(ubo.model, float4(input.position, 1.0))));
  output.color = input.color;
  return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
