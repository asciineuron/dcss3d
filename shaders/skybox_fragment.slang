// Skybox fragment shader — samples the cubemap using the interpolated 3D direction.

// Cube texture sampler — binding 0, space2
[[vk::binding(0, 2)]]
TextureCube cubeTexture : register(t0, space2);
[[vk::binding(1, 2)]]
SamplerState cubeSampler : register(s1, space2);

struct frag_in {
    float3 texCoords : TEXCOORD0;  // interpolated vertex position = direction vector
    float4 position  : SV_Position;
};

[shader("fragment")]
float4 main(frag_in input) : SV_Target0
{
    return cubeTexture.Sample(cubeSampler, input.texCoords);
}
