// Screen-space sprite vertex shader — instanced unit-quad rendering.
//
// Each sprite instance is a GpuSpriteData struct uploaded to a per-instance
// vertex buffer (slot 1). The base geometry is a unit quad (4 corners in slot 0).
// The vertex shader scales, rotates, and positions each instance in NDC space.
//
// Matches C++ GpuSpriteData layout (64 bytes):
//   float4 pos_rot    — posX, posY, posZ, rotation
//   float4 scale_uv1  — scaleX, scaleY, texU, texV
//   float4 uv2_color1 — texW, texH, r, g
//   float4 color2     — b, a, pad, pad

struct main_in {
    float2 corner     : TEXCOORD0;  // slot 0: unit quad corner (0,0)-(1,1)
    float4 pos_rot    : TEXCOORD1;  // slot 1: posXYZ + rotation
    float4 scale_uv1  : TEXCOORD2;  // slot 1: scaleXY + texUV top-left
    float4 uv2_color1 : TEXCOORD3;  // slot 1: texWH + color RG
    float4 color2     : TEXCOORD4;  // slot 1: color BA + padding
    uint id           : SV_InstanceID;
};

struct main_out {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

[shader("vertex")]
main_out main(main_in input)
{
    // Build the sprite quad from the unit corner
    float2 pos = input.corner;

    // Scale
    pos *= input.scale_uv1.xy;

    // Rotate
    float c = cos(input.pos_rot.w);
    float s = sin(input.pos_rot.w);
    float2x2 rot = { c, s, -s, c };
    pos = mul(pos, rot);

    // Translate to sprite position (NDC)
    pos += input.pos_rot.xy;

    // Output in NDC — z comes from pos_rot.z for draw ordering
    float4 clipPos = float4(pos, input.pos_rot.z, 1.0);

    // UV: top-left corner + corner offset * extent
    float2 uv = input.scale_uv1.zw + input.corner * input.uv2_color1.xy;

    main_out output;
    output.position = clipPos;
    output.texcoord = uv;
    output.color = float4(input.uv2_color1.z, input.uv2_color1.w,
                          input.color2.x, input.color2.y);
    return output;
}
