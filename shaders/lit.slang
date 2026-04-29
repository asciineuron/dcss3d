// Point light illumination — Blinn-Phong shading with texture support

// Light/camera position uniform — binding 0, space0
[[vk::binding(0)]]
cbuffer LightUBO : register(b0, space0)
{
    float4 lightPos;
    float4 cameraPos;
};

// Target cell highlight uniform — binding 1, space0
// xyz = world-space center of the targeted map cell, w = 1.0 if enabled, 0.0 if disabled
[[vk::binding(1)]]
cbuffer TargetUBO : register(b1, space0)
{
    float4 targetCellPos;
};

// Texture sampler — binding 0, space2 (space1 used for UBO in vertex shader)
[[vk::binding(0, 2)]]
Texture2D textureSampler : register(t0, space2);
[[vk::binding(1, 2)]]
SamplerState samplerState : register(s1, space2);

// Tile color passed from vertex shader via TEXCOORD4 (no SSBO read in fragment stage).
// This avoids Metal's buffer(0) conflict between LightUBO (buffer 0, space0) and any
// ByteAddressBuffer that also claims buffer 0.

struct frag_in {
    float3 worldPos    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float4 position    : SV_Position;
    float3 shiftData   : TEXCOORD2;  // from vertex shader
    float instanceId    : TEXCOORD3;  // from vertex shader
    float4 tileColor    : TEXCOORD4;  // tile color from per-instance vertex buffer
    float2 uv          : TEXCOORD5;  // texture UV coordinates from vertex shader
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

    // Sample texture if available
    float4 texColor = textureSampler.Sample(samplerState, input.uv);
    
    // If no valid texture data (shouldn't happen with proper textures), 
    // texColor alpha would be 0. Use tileColor as fallback.
    if (texColor.a < 0.001f) {
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

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
    
    // Modulate by texture color
    result *= texColor.rgb;

    // Target cell highlight: warm yellow glow on the cell the player is aiming at.
    // targetCellPos.w > 0.5 means highlighting is active.
    if (targetCellPos.w > 0.5f)
    {
        float cellHalfSize = 0.48f; // half of a 1x1 cell minus tiny margin
        float3 cellCenter = targetCellPos.xyz;
        bool inCell = abs(worldPos.x - cellCenter.x) < cellHalfSize
                   && abs(worldPos.z - cellCenter.z) < cellHalfSize
                   && worldPos.y > -1.0f;
        if (inCell)
        {
            // Boost ambient and add warm yellow overlay
            float3 highlightColor = float3(0.8f, 0.7f, 0.1f);
            result += highlightColor * 0.4f;
        }
    }
    
    return float4(result, tileColor.a * texColor.a);
}