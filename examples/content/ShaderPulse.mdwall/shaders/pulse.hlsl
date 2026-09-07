struct MiaoVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(MiaoVertexOutput input) : SV_Target
{
    const float2 resolution = max(MiaoResolution, float2(1.0, 1.0));
    float2 centered = input.uv * 2.0 - 1.0;
    centered.x *= resolution.x / resolution.y;

    const float radius = length(centered);
    const float wave = 0.5 + 0.5 * sin(MiaoTime * 1.7 - radius * 11.0);
    const float glow = exp(-radius * 2.4) * (0.55 + 0.45 * wave);

    float3 colorA = float3(0.08, 0.20, 0.55);
    float3 colorB = float3(0.50, 0.18, 0.95);
    float3 color = lerp(colorA, colorB, wave);
    color += float3(0.18, 0.52, 1.0) * glow;

    const float2 mouseUv = MiaoMousePosition / resolution;
    const float mouseGlow = exp(-distance(input.uv, mouseUv) * 10.0);
    color += float3(0.35, 0.70, 1.0) * mouseGlow * 0.35;

    return float4(color * MiaoObjectColor.rgb, MiaoObjectColor.a * MiaoObjectOpacity);
}
