#language slang 2026

module default;

struct VertexInput {
  float2 position;
  float3 color;
}

struct VertexOutput {
  float4 position : SV_Position;
  float3 color;
}

[shader("vertex")]
VertexOutput main(VertexInput input) {
  return {
    float4(input.position, 0.0, 1.0),
    input.color
  };
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
