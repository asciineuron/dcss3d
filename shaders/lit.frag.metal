#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 15 "shaders/lit.slang"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 15
struct pixelInput_0
{
    float3 worldPos_0 [[user(TEXCOORD)]];
    float3 worldNormal_0 [[user(TEXCOORD_1)]];
    float3 shiftData_0 [[user(TEXCOORD_2)]];
    float instanceId_0 [[user(TEXCOORD_3)]];
    float4 tileColor_0 [[user(TEXCOORD_4)]];
};


#line 5
struct SLANG_ParameterGroup_LightUBO_0
{
    float4 lightPos_0;
    float4 cameraPos_0;
};


#line 34
[[fragment]] pixelOutput_0 main_0(pixelInput_0 _S1 [[stage_in]], float4 position_0 [[position]], SLANG_ParameterGroup_LightUBO_0 constant* LightUBO_0 [[buffer(0)]])
{


    float3 normal_0 = normalize(_S1.worldNormal_0);


    float3 lightDir_0 = normalize(LightUBO_0->lightPos_0.xyz - _S1.worldPos_0);


    float dist_0 = length(LightUBO_0->lightPos_0.xyz - _S1.worldPos_0);


    float3 _S2 = _S1.tileColor_0.xyz;

#line 47
    pixelOutput_0 _S3 = { float4((float3(0.07999999821186066)  * _S2 + float3(max(dot(normal_0, lightDir_0), 0.0))  * _S2 + float3((0.30000001192092896 * pow(max(dot(normal_0, normalize(lightDir_0 + normalize(LightUBO_0->cameraPos_0.xyz - _S1.worldPos_0))), 0.0), 16.0))) ) * float3((1.0 / (1.0 + 0.09000000357627869 * dist_0 + 0.03200000151991844 * dist_0 * dist_0))) , _S1.tileColor_0.w) };

#line 57
    return _S3;
}

