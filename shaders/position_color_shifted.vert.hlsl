cbuffer UBO : register(b0, space1)
{
    float4x4 viewproj : packoffset(c0);
};

// do this per instance id
struct map_data {
	int x;
	int y;
	float4 color;
};

StructuredBuffer<map_data> data_buffer : register(t0, space0);

struct main_in {
	float3 position : TEXCOORD0;
	uint instance : SV_InstanceID;
};

struct main_out {
	float4 position : SV_Position;
};

main_out main(main_in input)
{
	map_data data = data_buffer[input.instance];
	// shift and use the appropriate color/texture

	main_out output;
	output.position = mul(viewproj, float4(input.position, 1.0f));
	return output;
}
