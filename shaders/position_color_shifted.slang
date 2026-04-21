#define IDENTITY_MATRIX float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

// from game.h
enum map_type {
    MTYPE_WALL,
    MTYPE_FLOOR,
    MTYPE_UNEXPLORED,
    MTYPE_OTHER,
    MTYPE_COUNT
};

// UBO for viewproj matrix — binding 0, space1
[[vk::binding(0)]]
cbuffer UBO : register(b0, space1)
{
    float4x4 viewproj : packoffset(c0);
};

// NOTE: data_buffer is NOT needed here — tile shift comes from a vertex attribute (TEXCOORD2).
// The tile type is encoded in shiftData.z and looked up by the fragment shader.

struct main_in {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 shiftData : TEXCOORD2;  // instance: tile_x, tile_y, type
    float4 colorData : TEXCOORD3; // instance: color RGBA
    uint id : SV_InstanceID;
};

struct main_out {
    float3 worldPos    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 position   : SV_Position;
    float3 shiftOut    : TEXCOORD2;  // pass through to fragment
    float instanceId   : TEXCOORD3;   // instance ID as float
    float4 tileColor   : TEXCOORD4;  // tile color from per-instance vertex attribute
};

[shader("vertex")]
main_out main(main_in input)
{
    float shiftY = 0.0f;
    if (int(input.shiftData.z) == map_type::MTYPE_FLOOR || int(input.shiftData.z) == map_type::MTYPE_UNEXPLORED)
        shiftY = -1.0f;

    float3 shift = { input.shiftData.x, shiftY, input.shiftData.y };
    float4x4 tile_translate = IDENTITY_MATRIX;
    tile_translate[0][3] = shift.x;
    tile_translate[1][3] = shift.y;
    tile_translate[2][3] = shift.z;

    float4 worldPos4 = mul(tile_translate, float4(input.position, 1.0f));

    main_out output;
    output.position = mul(viewproj, worldPos4);
    output.worldPos = worldPos4.xyz;
    output.worldNormal = input.normal;
    output.shiftOut = input.shiftData;
    output.instanceId = float(input.id);
    output.tileColor = input.colorData;
    return output;
}