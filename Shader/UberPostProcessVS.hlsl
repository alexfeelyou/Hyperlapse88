struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Generates a full-screen triangle without Vertex Buffers
VS_OUT main(uint vertexID : SV_VertexID)
{
    VS_OUT output;
    
    // Create UVs: (0,0), (2,0), (0,2)
    output.texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    
    // Create Positions: (-1,1), (3,1), (-1,-3)
    output.position = float4(output.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    
    return output;
}