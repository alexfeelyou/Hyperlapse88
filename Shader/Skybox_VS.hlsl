struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 worldPosition : WORLD_POSITION;
};

cbuffer SKYMAP_CONSTANT_BUFFER : register(b7)
{
    row_major float4x4 inverseViewProjection;
    float4 cameraPosition;
};

VS_OUT main(float3 position : POSITION)
{
    VS_OUT vout;
    
    // Output directly to NDC. Force Z to 1.0 to render at the far clip plane.
    vout.position = float4(position.xy, 1.0f, 1.0f);
    
    // Transform NDC back to World Space using the inverse matrix
    float4 worldPos = mul(vout.position, inverseViewProjection);
    vout.worldPosition = worldPos / worldPos.w;
    
    return vout;
}