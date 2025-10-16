cbuffer UBO : register(b0, space1)
{
    float4x4 viewproj : packoffset(c0);
};

struct main_in {
	float3 position : TEXCOORD0;
};

struct main_out {
	float4 position : SV_Position;
};

main_out main(main_in input)
{
	main_out output;
	output.position = mul(viewproj, float4(input.position, 1.0f));
	return output;
}
