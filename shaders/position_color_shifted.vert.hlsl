cbuffer UBO : register(b0, space1)
{
	float4x4 viewproj : packoffset(c0);
};

// do this per instance id
// struct map_data {
// 	int x;
// 	int y;
// 	float4 color;
// };
// StructuredBuffer<map_data> data_buffer : register(t0, space0);

// elem size: 24 bytes
ByteAddressBuffer data_buffer : register(t0, space0);

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
	float x = v.x, y = v.y, z = v.z;
	m[0][3] = x;
	m[1][3] = y;
	m[2][3] = z;
	return m;
}

main_out main(main_in input)
{
	// map_data data = data_buffer[input.id];
	// shift and use the appropriate color/texture
	int tile_x = asint(data_buffer.Load(24*input.id));
	int tile_y = asint(data_buffer.Load(4 + 24*input.id));
	float4 color = asfloat(data_buffer.Load4(8 + 24*input.id));

	float4x4 tile_translate;
	float3 shift = {tile_x, tile_y, 0};
	m_translate(tile_translate, shift);

	main_out output;
	output.position = mul(viewproj, mul(tile_translate, float4(input.position, 1.0f)));
	output.color = color;
	return output;
}
