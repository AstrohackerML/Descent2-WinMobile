// Shared by the fixed-function emulation shaders in gles_d3d11.cpp.

cbuffer DrawState : register(b0)
{
	float4x4 mvp;		// projection * modelview, column-major like GL
	float alpha_ref;	// GL_ALPHA_TEST with GL_GEQUAL; -1 when disabled
	float flip_v;		// 1 for textures copied from the framebuffer
	float2 padding;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};
