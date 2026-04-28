// Skybox vertex shader — renders a unit cube centered at origin.
// Outputs the local vertex position as the 3D texture direction for the cubemap.
// Uses the xyww trick so that after perspective divide, z=1.0 (max depth),
// allowing the skybox to be rendered LAST with LESS_OR_EQUAL depth test.

#define IDENTITY_MATRIX float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

// UBO for viewproj matrix (no translation) — binding 0, space1
[[vk::binding(0)]]
cbuffer SkyboxUBO : register(b0, space1)
{
    float4x4 viewproj : packoffset(c0);  // projection * view(rotation only, no translation)
};

struct main_in {
    float3 position : TEXCOORD0;
    uint id : SV_VertexID;
};

struct main_out {
    float3 texCoords  : TEXCOORD0; // local position = cubemap sampling direction
    float4 position   : SV_Position;
};

[shader("vertex")]
main_out main(main_in input)
{
    // Transform the unit cube vertex by viewproj (rotation+projection only, no translation).
    // This keeps the skybox centered on the camera — infinite distance illusion.
    float4 worldPos = mul(viewproj, float4(input.position, 1.0f));

    main_out output;
    output.texCoords = input.position;
    // xyww trick: set z = w so that after perspective divide z = 1.0 (maximum depth).
    // Combined with LESS_OR_EQUAL depth test, skybox only renders where no scene geometry is.
    output.position = worldPos.xyww;
    return output;
}
