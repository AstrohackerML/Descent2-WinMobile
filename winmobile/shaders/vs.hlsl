#include "common.hlsli"

struct VertexIn
{
	float3 pos : POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

VertexOut main(VertexIn v)
{
	VertexOut o;
	o.pos = mul(mvp, float4(v.pos, 1.0f));
	// GL clip space has z in [-w, w], Direct3D wants [0, w]
	o.pos.z = (o.pos.z + o.pos.w) * 0.5f;
	o.color = v.color;
	o.uv = float2(v.uv.x, lerp(v.uv.y, 1.0f - v.uv.y, flip_v));
	return o;
}
