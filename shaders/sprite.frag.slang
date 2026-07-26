// Screen-space sprite fragment shader — simple texture sample with alpha blending.
//
// Samples the sprite atlas texture at the UV coordinates passed from the
// vertex shader and multiplies by the per-instance color.

[[vk::binding(0, 2)]]
Texture2D spriteTexture : register(t0, space2);
[[vk::binding(1, 2)]]
SamplerState spriteSampler : register(s1, space2);

struct frag_in {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

[shader("fragment")]
float4 main(frag_in input) : SV_Target0
{
    float4 texColor = spriteTexture.Sample(spriteSampler, input.texcoord);
    return texColor * input.color;
}
