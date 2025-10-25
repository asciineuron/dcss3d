#define IDENTITY_MATRIX float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)

// from game.h
enum map_type {
	MTYPE_NONE,
	MTYPE_WALL,
	MTYPE_FLOOR,
	MTYPE_UNKNOWN,
	MTYPE_COUNT
};

cbuffer UBO : register(b0, space1)
{
	float4x4 viewproj : packoffset(c0);
};

// elem size: 32 bytes, see below
ByteAddressBuffer data_buffer : register(t0, space2);

struct main_in {
	float3 position : TEXCOORD0;
	uint id : SV_InstanceID;
};

struct main_out {
	float4 color : TEXCOORD0;
	float4 position : SV_Position;
};

float4x4 m_translate(float4x4 m, float3 v)
{
	// float x = v.x, y = v.y, z = v.z;
	m[0][3] = v.x;
	m[1][3] = v.y;
	m[2][3] = v.z;
	return m;
}

main_out main(main_in input)
{
	// shift and use the appropriate color/texture
	// see “GLSL std140 layout”, buffer needs 16 byte alignment
	float tile_x = asfloat(data_buffer.Load(32 * input.id));
	float tile_y = asfloat(data_buffer.Load(4 + 32 * input.id));
	float tile_z = asfloat(data_buffer.Load(8 + 32 * input.id));
	int type = asint(data_buffer.Load(12 + 32 * input.id));
	float4 color = asfloat(data_buffer.Load4(16 + 32 * input.id));

	float3 shift = { tile_x, 0.0f, tile_y };
	if (type == MTYPE_FLOOR)
		shift.y = -2.0f;

	float4x4 tile_translate = IDENTITY_MATRIX;
	tile_translate = m_translate(tile_translate, shift);

	main_out output;
	output.position = mul(viewproj, mul(tile_translate,
					    float4(input.position, 1.0f)));
	output.color = color;
	return output;
}
