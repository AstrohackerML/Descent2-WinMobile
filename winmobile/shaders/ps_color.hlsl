#include "common.hlsli"

float4 main(VertexOut i) : SV_TARGET
{
	clip(i.color.a - alpha_ref);
	return i.color;
}
