cbuffer CbScene : register(b0)
{
    row_major float4x4 viewProjection;
};

// Data Statis: Bentuk 1 Jendela (Quad)
struct VS_IN
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

// Data Dinamis: Info unik tiap jendela (dikirim tiap frame)
struct INSTANCE_IN
{
    float3 instPos : INST_POS;
    float2 instSize : INST_SIZE;
    float4 instColor : INST_COL;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

VS_OUT main(VS_IN vin, INSTANCE_IN inst)
{
    VS_OUT vout;
    
    // 1. Skala ukuran jendela (-0.5 s/d 0.5) dikali instSize
    float3 scaledPos = vin.position;
    scaledPos.x *= inst.instSize.x;
    scaledPos.y *= inst.instSize.y;
    
    // 2. Pindahkan ke posisinya di dunia 3D
    float3 worldPos = scaledPos + inst.instPos;
    
    // 3. Proyeksikan ke layar kamera
    vout.position = mul(float4(worldPos, 1.0f), viewProjection);
    vout.texcoord = vin.texcoord;
    vout.color = inst.instColor;
    
    return vout;
}