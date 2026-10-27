#include "common.hlsli"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

// GL_MODULATE: texture color times vertex color
float4 main(VertexOut i) : SV_TARGET
{
	float4 c = tex.Sample(samp, i.uv) * i.color;
	clip(c.a - alpha_ref);
	return c;
}
