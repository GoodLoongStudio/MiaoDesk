struct MiaoVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(MiaoVertexOutput input) : SV_Target
{
    // Parameter packing v1 follows parameters.json order:
    // [0].x opacity (also bound to SpriteRenderer.opacity)
    // [1].x speed
    // [2].x intensity
    // [3]   accentColor
    const float speed = MiaoParameter[1].x;
    const float intensity = MiaoParameter[2].x;
    const float3 accent = MiaoParameter[3].rgb;

    const float2 resolution = max(MiaoResolution, float2(1.0, 1.0));
    float2 centered = input.uv * 2.0 - 1.0;
    centered.x *= resolution.x / resolution.y;

    const float radius = length(centered);
    const float wave = 0.5 + 0.5 * sin(MiaoTime * speed - radius * 11.0);
    const float glow = exp(-radius * 2.4) * (0.55 + 0.45 * wave) * intensity;

    float3 colorA = float3(0.08, 0.20, 0.55);
    float3 colorB = float3(0.50, 0.18, 0.95);
    float3 color = lerp(colorA, colorB, wave);
    color += accent * glow;

    const float2 mouseUv = MiaoMousePosition / resolution;
    const float mouseGlow = exp(-distance(input.uv, mouseUv) * 10.0);
    color += accent * mouseGlow * 0.35 * intensity;

    return float4(color * MiaoObjectColor.rgb, MiaoObjectColor.a * MiaoObjectOpacity);
}
