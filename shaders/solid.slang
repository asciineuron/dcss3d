// Point light illumination — Blinn-Phong shading, solid color (no texture).
// Used for monster MVP rendering.

// Light/camera position uniform — binding 0, space0
[[vk::binding(0)]]
cbuffer LightUBO : register(b0, space0)
{
    float4 lightPos;
    float4 cameraPos;
};

// Tile color passed from vertex shader via TEXCOORD4.
// No texture sampler needed — just the per-instance color.

struct frag_in {
    float3 worldPos    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 position    : SV_Position;
    float3 shiftData   : TEXCOORD2;
    float instanceId    : TEXCOORD3;
    float4 tileColor    : TEXCOORD4;
    float2 uv          : TEXCOORD5;
};

// Phong lighting parameters
static const float AMBIENT_STRENGTH = 0.25;
static const float SPECULAR_STRENGTH = 0.3;
static const float SHININESS = 16.0;

static const float ATTEN_CONSTANT = 1.0;
static const float ATTEN_LINEAR = 0.09;
static const float ATTEN_QUADRATIC = 0.032;

[shader("fragment")]
float4 main(frag_in input) : SV_Target0
{
    float4 tileColor = input.tileColor;

    float3 normal = normalize(input.worldNormal);
    float3 worldPos = input.worldPos;

    float3 lightDir = normalize(lightPos.xyz - worldPos);
    float3 viewDir = normalize(cameraPos.xyz - worldPos);

    float dist = length(lightPos.xyz - worldPos);
    float attenuation = 1.0 / (ATTEN_CONSTANT + ATTEN_LINEAR * dist + ATTEN_QUADRATIC * dist * dist);

    float3 ambient = AMBIENT_STRENGTH * tileColor.rgb;

    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = diff * tileColor.rgb * 1.2;

    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), SHININESS);
    float3 specular = SPECULAR_STRENGTH * spec * float3(1.0, 1.0, 1.0);

    float3 result = (ambient + diffuse + specular) * attenuation;

    return float4(result, tileColor.a);
}
