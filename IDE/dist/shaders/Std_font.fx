Texture2D tex2D;
SamplerState linearSampler
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

float4 WindowSize;

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
	
	output.Color = input.Color;
	output.Tex = input.Tex;
	
	output.Color = float4(input.Color.b * input.Color.a, input.Color.g * input.Color.a, input.Color.r * input.Color.a, input.Color.a);

    return output;  
}

float4 PS(PS_INPUT input) : SV_Target
{
	float4 aTex = tex2D.Sample(linearSampler, input.Tex);
	float aGrey = input.Color.r * 0.299 + input.Color.g * 0.587 + input.Color.b * 0.114;
	float a = lerp(aTex.a, aTex.r, aGrey);
    return float4(a, a, a, a) * input.Color;
}
