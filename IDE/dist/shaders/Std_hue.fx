Texture2D tex2D;
SamplerState linearSampler
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

float4 WindowSize : register(vs, c0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;    
    float2 Tex : TEXCOORD;
	float4 Color : COLOR;
};

struct VS_INPUT
{
	float4 Pos : POSITION;    
    float2 Tex : TEXCOORD;
	float4 Color : COLOR;
};

PS_INPUT VS(VS_INPUT input)
{
	PS_INPUT output;
	
	output.Pos = float4(input.Pos.x / WindowSize.x * 2 - 1, 1 - input.Pos.y / WindowSize.y * 2, 0, 1);
	output.Tex = input.Tex;	
	output.Color = float4(input.Color.b * input.Color.a, input.Color.g * input.Color.a, input.Color.r * input.Color.a, input.Color.a);
    return output;  
}

float4x4 ColorMatrix : register(ps, c0);

float4 PS(PS_INPUT input) : SV_Target
{
    float4 col = tex2D.Sample(linearSampler, input.Tex) * input.Color; 	

    col = mul(col, ColorMatrix);
	
	//col += float4(0.1, 0.1, 0.1, 0.1);

    return col;
}
