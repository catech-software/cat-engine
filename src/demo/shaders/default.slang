#language slang 2026

module default;

struct VertexOutput {
  float4 position : SV_Position;
  float3 color;
}

static float2 positions[6] = float2[](
  float2(-0.5, -0.5),
  float2(-0.5,  0.5),
  float2( 0.5,  0.5),
  float2( 0.5,  0.5),
  float2( 0.5, -0.5),
  float2(-0.5, -0.5)
);

static float3 colors[6] = float3[](
  float3(0.0, 0.0, 0.0),
  float3(0.0, 0.0, 1.0),
  float3(0.0, 1.0, 1.0),
  float3(0.0, 1.0, 1.0),
  float3(0.0, 1.0, 0.0),
  float3(0.0, 0.0, 0.0)
);

[shader("vertex")]
VertexOutput main(uint id : SV_VertexID) {
  return {
    float4(positions[id], 0.0, 1.0),
    colors[id]
  };
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
