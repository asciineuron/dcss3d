#define IDENTITY_MATRIX float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

// Standalone monster vertex shader — no floor-sink logic, no sentinel hack.
// Computes per-instance look-at-player rotation in the XZ plane.

// UBO for viewproj matrix — binding 0, space1
[[vk::binding(0)]]
cbuffer UBO : register(b0, space1)
{
    float4x4 viewproj : packoffset(c0);
};

// Player 2D position (render coords: x,z) for look-at rotation — binding 1, space1
[[vk::binding(1)]]
cbuffer MonsterUBO : register(b1, space1)
{
    float4 playerPos;  // .xy = player render coords (x, z), .zw unused
};

struct main_in {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 shiftData : TEXCOORD2;  // instance: render_x, render_z, tileType (unused here)
    float4 colorData : TEXCOORD3;  // instance: color RGBA
    float2 uv        : TEXCOORD4;  // texture UV coordinates
    uint id : SV_InstanceID;
};

struct main_out {
    float3 worldPos    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 position   : SV_Position;
    float3 shiftOut    : TEXCOORD2;  // pass through to fragment
    float instanceId   : TEXCOORD3;   // instance ID as float
    float4 tileColor   : TEXCOORD4;  // tile color from per-instance vertex attribute
    float2 uv          : TEXCOORD5;  // texture UV coordinates
};

[shader("vertex")]
main_out main(main_in input)
{
    // Monsters sit on the floor (bottom of cell, y=-0.5).
    float shiftY = -0.5f;

    // Compute look-at-player rotation in the XZ plane.
    // shiftData.xy = monster render position, playerPos.xy = player render position.
    float2 toPlayer = normalize(playerPos.xy - input.shiftData.xy);
    // Model faces +Z. Rotation that maps +Z → (dx, dz):  cos=dz, sin=-dx.
    float cosA = toPlayer.y;   // dz
    float sinA = -toPlayer.x;  // -dx

    // Build 4x4 rotation matrix in XZ plane (around Y axis):
    //   [ cosA   0  -sinA   0 ]
    //   [   0    1     0    0 ]
    //   [ sinA   0   cosA   0 ]
    //   [   0    0     0    1 ]
    float4x4 rot = IDENTITY_MATRIX;
    rot[0][0] = cosA;   rot[0][2] = -sinA;
    rot[2][0] = sinA;   rot[2][2] = cosA;

    // Build translation matrix
    float3 shift = { input.shiftData.x, shiftY, input.shiftData.y };
    float4x4 tile_translate = IDENTITY_MATRIX;
    tile_translate[0][3] = shift.x;
    tile_translate[1][3] = shift.y;
    tile_translate[2][3] = shift.z;

    // Transform: rotate first, then translate
    float4 worldPos4 = mul(tile_translate, mul(rot, float4(input.position, 1.0f)));

    // Rotate the normal too (w=0 implicitly, so translation is skipped)
    float3 worldNormal = mul((float3x3)rot, input.normal);

    main_out output;
    output.position = mul(viewproj, worldPos4);
    output.worldPos = worldPos4.xyz;
    output.worldNormal = worldNormal;
    output.shiftOut = input.shiftData;
    output.instanceId = float(input.id);
    output.tileColor = input.colorData;
    output.uv = input.uv;
    return output;
}
