#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 22 "shaders/position_color_shifted.slang"
struct main_Result_0
{
    float3 worldPos_0 [[user(TEXCOORD)]];
    float3 worldNormal_0 [[user(TEXCOORD_1)]];
    float4 position_0 [[position]];
    float3 shiftOut_0 [[user(TEXCOORD_2)]];
    float instanceId_0 [[user(TEXCOORD_3)]];
    float4 tileColor_0 [[user(TEXCOORD_4)]];
};


#line 22
struct vertexInput_0
{
    float3 position_1 [[attribute(0)]];
    float3 normal_0 [[attribute(1)]];
    float3 shiftData_0 [[attribute(2)]];
    float4 colorData_0 [[attribute(3)]];
};


#line 22
struct _MatrixStorage_float4x4_ColMajornatural_0
{
    array<float4, int(4)> data_0;
};


#line 22
struct SLANG_ParameterGroup_UBO_natural_0
{
    _MatrixStorage_float4x4_ColMajornatural_0 viewproj_0;
};


#line 993 "core"
struct KernelContext_0
{
    SLANG_ParameterGroup_UBO_natural_0 constant* UBO_0;
};


#line 30 "shaders/position_color_shifted.slang"
struct main_out_0
{
    float3 worldPos_1;
    float3 worldNormal_1;
    float4 position_2;
    float3 shiftOut_1;
    float instanceId_1;
    float4 tileColor_1;
};


#line 30
[[vertex]] main_Result_0 main_0(vertexInput_0 _S1 [[stage_in]], uint id_0 [[instance_id]], SLANG_ParameterGroup_UBO_natural_0 constant* UBO_1 [[buffer(0)]])
{

#line 30
    thread KernelContext_0 kernelContext_0;

#line 30
    (&kernelContext_0)->UBO_0 = UBO_1;

#line 43
    int _S2 = int(_S1.shiftData_0.z);

#line 43
    bool _S3;

#line 43
    if(_S2 == int(1))
    {

#line 43
        _S3 = true;

#line 43
    }
    else
    {

#line 43
        _S3 = _S2 == int(2);

#line 43
    }

#line 43
    float shiftY_0;

#line 43
    if(_S3)
    {

#line 43
        shiftY_0 = -1.0;

#line 43
    }
    else
    {

#line 43
        shiftY_0 = 0.0;

#line 43
    }


    float _S4 = _S1.shiftData_0.x;

#line 46
    float _S5 = _S1.shiftData_0.y;
    thread matrix<float,int(4),int(4)>  tile_translate_0 = matrix<float,int(4),int(4)> (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
    tile_translate_0[int(0)].w = _S4;
    tile_translate_0[int(1)].w = shiftY_0;
    tile_translate_0[int(2)].w = _S5;

    float4 worldPos4_0 = (((float4(_S1.position_1, 1.0)) * (tile_translate_0)));

    thread main_out_0 output_0;
    (&output_0)->position_2 = (((worldPos4_0) * (matrix<float,int(4),int(4)> ((&kernelContext_0)->UBO_0->viewproj_0.data_0[int(0)][int(0)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(1)][int(0)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(2)][int(0)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(3)][int(0)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(0)][int(1)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(1)][int(1)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(2)][int(1)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(3)][int(1)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(0)][int(2)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(1)][int(2)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(2)][int(2)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(3)][int(2)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(0)][int(3)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(1)][int(3)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(2)][int(3)], (&kernelContext_0)->UBO_0->viewproj_0.data_0[int(3)][int(3)]))));
    (&output_0)->worldPos_1 = worldPos4_0.xyz;
    (&output_0)->worldNormal_1 = _S1.normal_0;
    (&output_0)->shiftOut_1 = _S1.shiftData_0;
    (&output_0)->instanceId_1 = float(id_0);
    (&output_0)->tileColor_1 = _S1.colorData_0;
    main_out_0 _S6 = output_0;

#line 61
    thread main_Result_0 _S7;

#line 61
    (&_S7)->worldPos_0 = _S6.worldPos_1;

#line 61
    (&_S7)->worldNormal_0 = _S6.worldNormal_1;

#line 61
    (&_S7)->position_0 = _S6.position_2;

#line 61
    (&_S7)->shiftOut_0 = _S6.shiftOut_1;

#line 61
    (&_S7)->instanceId_0 = _S6.instanceId_1;

#line 61
    (&_S7)->tileColor_0 = _S6.tileColor_1;

#line 61
    return _S7;
}

