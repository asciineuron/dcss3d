#pragma pack_matrix(column_major)
#ifdef SLANG_HLSL_ENABLE_NVAPI
#include "nvHLSLExtns.h"
#endif

#ifndef __DXC_VERSION_MAJOR
// warning X3557: loop doesn't seem to do anything, forcing loop to unroll
#pragma warning(disable : 3557)
#endif


#line 14 "shaders/position_color_shifted.slang"
struct SLANG_ParameterGroup_UBO_0
{
    float4x4 viewproj_0;
};


#line 14
cbuffer UBO_0 : register(b0, space1)
{
    float4x4 viewproj_0 : packoffset(c0);
}

#line 30
struct main_out_0
{
    float3 worldPos_0 : TEXCOORD0;
    float3 worldNormal_0 : TEXCOORD1;
    float4 position_0 : SV_Position;
    float3 shiftOut_0 : TEXCOORD2;
    float instanceId_0 : TEXCOORD3;
    float4 tileColor_0 : TEXCOORD4;
};


#line 22
struct main_in_0
{
    float3 position_1 : TEXCOORD0;
    float3 normal_0 : TEXCOORD1;
    float3 shiftData_0 : TEXCOORD2;
    float4 colorData_0 : TEXCOORD3;
    uint id_0 : SV_InstanceID;
};


#line 40
main_out_0 main(main_in_0 input_0)
{

#line 40
    bool _S1;


    if(int(input_0.shiftData_0.z) == int(1))
    {

#line 43
        _S1 = true;

#line 43
    }
    else
    {

#line 43
        _S1 = int(input_0.shiftData_0.z) == int(2);

#line 43
    }

#line 43
    float shiftY_0;

#line 43
    if(_S1)
    {

#line 43
        shiftY_0 = -1.0f;

#line 43
    }
    else
    {

#line 43
        shiftY_0 = 0.0f;

#line 43
    }


    float _S2 = input_0.shiftData_0.x;

#line 46
    float _S3 = input_0.shiftData_0.y;
    float4x4 tile_translate_0 = float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    tile_translate_0[int(0)][int(3)] = _S2;
    tile_translate_0[int(1)][int(3)] = shiftY_0;
    tile_translate_0[int(2)][int(3)] = _S3;

    float4 worldPos4_0 = mul(tile_translate_0, float4(input_0.position_1, 1.0f));

    main_out_0 output_0;
    output_0.position_0 = mul(viewproj_0, worldPos4_0);
    output_0.worldPos_0 = worldPos4_0.xyz;
    output_0.worldNormal_0 = input_0.normal_0;
    output_0.shiftOut_0 = input_0.shiftData_0;
    output_0.instanceId_0 = float(input_0.id_0);
    output_0.tileColor_0 = input_0.colorData_0;
    return output_0;
}

