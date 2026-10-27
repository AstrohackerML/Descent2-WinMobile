// OpenGL ES 1.x fixed-function emulation on Direct3D 11, covering only what
// the Descent renderer calls. See gles_d3d11.h.

#include "gles_d3d11.h"

#include <GLES/gl.h>

#include <wrl/client.h>
#include <map>
#include <mutex>
#include <string.h>
#include <vector>

#include "vs.h"
#include "ps_color.h"
#include "ps_texture.h"

using Microsoft::WRL::ComPtr;

namespace {

struct Vertex {
	float pos[3];
	float color[4];
	float uv[2];
};

struct DrawState {
	float mvp[16];
	float alpha_ref;
	float flip_v;
	float padding[2];
};

struct Texture {
	ComPtr<ID3D11Texture2D> tex;
	ComPtr<ID3D11ShaderResourceView> srv;
	GLint min_filter = GL_LINEAR;
	GLint mag_filter = GL_LINEAR;
	GLint wrap_s = GL_REPEAT;
	GLint wrap_t = GL_REPEAT;
	// Rows of textures copied from the framebuffer run top-down, GL expects bottom-up
	bool flip_v = false;
};

struct ClientArray {
	bool enabled = false;
	GLint size = 0;
	GLenum type = GL_FLOAT;
	GLsizei stride = 0;
	const GLubyte *pointer = nullptr;
};

const UINT kVertexBufferSize = 65536;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<IDXGISwapChain1> swap_chain;
ComPtr<ID3D11Texture2D> target;
ComPtr<ID3D11RenderTargetView> target_view;
ComPtr<ID3D11ShaderResourceView> target_srv;
ComPtr<ID3D11DepthStencilView> depth_view;
ComPtr<ID3D11VertexShader> vertex_shader;
ComPtr<ID3D11PixelShader> color_shader;
ComPtr<ID3D11PixelShader> texture_shader;
ComPtr<ID3D11InputLayout> input_layout;
ComPtr<ID3D11Buffer> vertex_buffer;
ComPtr<ID3D11Buffer> state_buffer;
UINT vertex_buffer_pos;
int target_w, target_h;

// The phone's display is natively portrait; the compositor shows the swap
// chain in that orientation unless told how the content is turned
struct WindowState {
	int width, height, rotation;

	bool operator!=(const WindowState &o) const {
		return width != o.width || height != o.height || rotation != o.rotation;
	}
};
std::mutex window_lock;
WindowState wanted_window = {1, 1, DXGI_MODE_ROTATION_IDENTITY};
WindowState swap_chain_window;

std::map<GLuint, Texture> textures;
GLuint next_texture = 1;
GLuint bound_texture;

std::map<UINT, ComPtr<ID3D11SamplerState>> samplers;
std::map<UINT, ComPtr<ID3D11BlendState>> blend_states;
std::map<UINT, ComPtr<ID3D11DepthStencilState>> depth_states;
std::map<UINT, ComPtr<ID3D11RasterizerState>> raster_states;

float matrices[2][16];	// GL_MODELVIEW, GL_PROJECTION; column-major like GL
int matrix_mode;
float current_color[4] = {1, 1, 1, 1};
float clear_color[4];
bool texture_2d, alpha_test, depth_test, blend, cull_face;
float alpha_ref;
GLenum depth_func = GL_LESS;
GLenum cull_mode = GL_BACK;
GLenum blend_src = GL_ONE, blend_dst = GL_ZERO;
ClientArray vertex_array, color_array, texcoord_array;
D3D11_VIEWPORT viewport;

std::vector<Vertex> scratch;

void load_identity(float *m) {
	memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b, all column-major
void multiply(float *out, const float *a, const float *b) {
	float r[16];
	for (int c = 0; c < 4; ++c) {
		for (int row = 0; row < 4; ++row) {
			float sum = 0;
			for (int k = 0; k < 4; ++k) {
				sum += a[k * 4 + row] * b[c * 4 + k];
			}
			r[c * 4 + row] = sum;
		}
	}
	memcpy(out, r, sizeof(r));
}

void multiply_current(const float *m) {
	multiply(matrices[matrix_mode], matrices[matrix_mode], m);
}

D3D11_BLEND to_d3d_blend(GLenum factor) {
	switch (factor) {
		case GL_ZERO: return D3D11_BLEND_ZERO;
		case GL_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
		case GL_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
		default: return D3D11_BLEND_ONE;
	}
}

D3D11_COMPARISON_FUNC to_d3d_compare(GLenum func) {
	return (D3D11_COMPARISON_FUNC) (func - GL_NEVER + D3D11_COMPARISON_NEVER);
}

ID3D11SamplerState *get_sampler(const Texture &t) {
	UINT key = (t.min_filter == GL_LINEAR) | (t.mag_filter == GL_LINEAR) << 1 |
			   (t.wrap_s == GL_CLAMP_TO_EDGE) << 2 | (t.wrap_t == GL_CLAMP_TO_EDGE) << 3;
	ComPtr<ID3D11SamplerState> &s = samplers[key];
	if (!s) {
		static const D3D11_FILTER filters[] = {
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
			D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
			D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
		};
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = filters[key & 3];
		desc.AddressU = key & 4 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = key & 8 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		device->CreateSamplerState(&desc, &s);
	}
	return s.Get();
}

ID3D11BlendState *get_blend_state() {
	UINT key = blend ? (blend_src << 16 | blend_dst) + 1 : 0;
	ComPtr<ID3D11BlendState> &s = blend_states[key];
	if (!s) {
		D3D11_BLEND_DESC desc = {};
		D3D11_RENDER_TARGET_BLEND_DESC &rt = desc.RenderTarget[0];
		rt.BlendEnable = blend;
		rt.SrcBlend = rt.SrcBlendAlpha = to_d3d_blend(blend_src);
		rt.DestBlend = rt.DestBlendAlpha = to_d3d_blend(blend_dst);
		rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		device->CreateBlendState(&desc, &s);
	}
	return s.Get();
}

ID3D11DepthStencilState *get_depth_state() {
	UINT key = depth_test ? depth_func : 0;
	ComPtr<ID3D11DepthStencilState> &s = depth_states[key];
	if (!s) {
		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = depth_test;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = to_d3d_compare(depth_func);
		desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp =
				desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		desc.BackFace = desc.FrontFace;
		device->CreateDepthStencilState(&desc, &s);
	}
	return s.Get();
}

ID3D11RasterizerState *get_raster_state() {
	UINT key = cull_face ? cull_mode : 0;
	ComPtr<ID3D11RasterizerState> &s = raster_states[key];
	if (!s) {
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = !cull_face ? D3D11_CULL_NONE :
						cull_mode == GL_FRONT ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
		desc.FrontCounterClockwise = TRUE;	// GL_CCW, the GL default
		desc.DepthClipEnable = TRUE;
		device->CreateRasterizerState(&desc, &s);
	}
	return s.Get();
}

void read_array(const ClientArray &a, GLint index, float *out) {
	GLsizei stride = a.stride;
	if (a.type == GL_UNSIGNED_BYTE) {
		const GLubyte *p = a.pointer + index * (stride ? stride : a.size);
		for (int i = 0; i < a.size; ++i) {
			out[i] = p[i] / 255.0f;
		}
	} else {
		const float *p = (const float *) (a.pointer +
										  index * (stride ? stride : a.size * sizeof(float)));
		for (int i = 0; i < a.size; ++i) {
			out[i] = p[i];
		}
	}
}

Vertex fetch_vertex(GLint index) {
	Vertex v = {};
	read_array(vertex_array, index, v.pos);
	if (color_array.enabled) {
		read_array(color_array, index, v.color);
	} else {
		memcpy(v.color, current_color, sizeof(v.color));
	}
	if (texcoord_array.enabled) {
		read_array(texcoord_array, index, v.uv);
	}
	return v;
}

Texture *find_texture(GLuint id) {
	auto it = textures.find(id);
	return it == textures.end() ? nullptr : &it->second;
}

bool create_texture(Texture &t, int w, int h, const void *pixels) {
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data = {pixels, (UINT) w * 4, 0};
	t.tex.Reset();
	t.srv.Reset();
	t.flip_v = false;
	if (FAILED(device->CreateTexture2D(&desc, pixels ? &data : nullptr, &t.tex))) {
		return false;
	}
	return SUCCEEDED(device->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv));
}

// A box of the render target in Direct3D coordinates for a GL window rectangle
D3D11_BOX target_box(GLint x, GLint y, GLsizei w, GLsizei h) {
	D3D11_BOX box;
	box.left = (UINT) max(x, 0);
	box.right = (UINT) min(x + w, target_w);
	box.top = (UINT) max(target_h - y - h, 0);
	box.bottom = (UINT) min(target_h - y, target_h);
	box.front = 0;
	box.back = 1;
	return box;
}

bool create_target() {
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = target_w;
	desc.Height = target_h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &target)) ||
		FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)) ||
		FAILED(device->CreateShaderResourceView(target.Get(), nullptr, &target_srv))) {
		return false;
	}

	ComPtr<ID3D11Texture2D> depth;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &depth)) ||
		FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &depth_view))) {
		return false;
	}

	const float black[4] = {0, 0, 0, 1};
	context->ClearRenderTargetView(target_view.Get(), black);
	context->ClearDepthStencilView(depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	return true;
}

bool create_pipeline() {
	if (FAILED(device->CreateVertexShader(g_vs, sizeof(g_vs), nullptr, &vertex_shader)) ||
		FAILED(device->CreatePixelShader(g_ps_color, sizeof(g_ps_color), nullptr,
										 &color_shader)) ||
		FAILED(device->CreatePixelShader(g_ps_texture, sizeof(g_ps_texture), nullptr,
										 &texture_shader))) {
		return false;
	}

	const D3D11_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	if (FAILED(device->CreateInputLayout(layout, ARRAYSIZE(layout), g_vs, sizeof(g_vs),
										 &input_layout))) {
		return false;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = kVertexBufferSize * sizeof(Vertex);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&desc, nullptr, &vertex_buffer))) {
		return false;
	}
	desc.ByteWidth = sizeof(DrawState);
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &state_buffer));
}

// Appends the vertices in scratch to the vertex buffer
bool upload_scratch(UINT *start) {
	D3D11_MAPPED_SUBRESOURCE mapped;
	D3D11_MAP map_type;
	UINT n = (UINT) scratch.size();

	if (n == 0 || n > kVertexBufferSize) {
		return false;
	}
	if (vertex_buffer_pos + n > kVertexBufferSize) {
		vertex_buffer_pos = 0;
		map_type = D3D11_MAP_WRITE_DISCARD;
	} else {
		map_type = vertex_buffer_pos ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
	}
	if (FAILED(context->Map(vertex_buffer.Get(), 0, map_type, 0, &mapped))) {
		return false;
	}
	memcpy((Vertex *) mapped.pData + vertex_buffer_pos, scratch.data(), n * sizeof(Vertex));
	context->Unmap(vertex_buffer.Get(), 0);
	*start = vertex_buffer_pos;
	vertex_buffer_pos += n;
	return true;
}

void set_draw_state(const DrawState &state) {
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(state_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &state, sizeof(state));
		context->Unmap(state_buffer.Get(), 0);
	}
}

bool swaps_axes(int rotation) {
	return rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270;
}

// The point of the offscreen target that shows at (u, v) of the back buffer,
// both in [0, 1] from the top left, when the window is turned by rotation
// relative to the display
void rotated_uv(int rotation, float u, float v, float *out) {
	switch (rotation) {
		case DXGI_MODE_ROTATION_ROTATE90: out[0] = 1 - v; out[1] = u; break;
		case DXGI_MODE_ROTATION_ROTATE180: out[0] = 1 - u; out[1] = 1 - v; break;
		case DXGI_MODE_ROTATION_ROTATE270: out[0] = v; out[1] = 1 - u; break;
		default: out[0] = u; out[1] = v; break;
	}
}

WindowState get_wanted_window() {
	std::lock_guard<std::mutex> lock(window_lock);
	return wanted_window;
}

bool is_portrait(const WindowState &w) {
	return w.width < w.height;
}

// The landscape frame in a portrait window is shown as in a landscape one
int frame_rotation(const WindowState &w) {
	int quarter_turns = w.rotation - DXGI_MODE_ROTATION_IDENTITY + (is_portrait(w) ? 3 : 0);
	return DXGI_MODE_ROTATION_IDENTITY + quarter_turns % 4;
}

// The swap chain keeps the display's native shape
void native_size(const WindowState &w, UINT *width, UINT *height) {
	*width = swaps_axes(w.rotation) ? w.height : w.width;
	*height = swaps_axes(w.rotation) ? w.width : w.height;
}

}

bool gles_init(IUnknown *core_window, int width, int height) {
	const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,
	};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
								 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
								 D3D11_SDK_VERSION, &device, nullptr, &context))) {
		return false;
	}

	ComPtr<IDXGIDevice1> dxgi_device;
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<IDXGIFactory2> factory;
	device.As(&dxgi_device);
	dxgi_device->GetAdapter(&adapter);
	adapter->GetParent(IID_PPV_ARGS(&factory));
	dxgi_device->SetMaximumFrameLatency(1);

	swap_chain_window = get_wanted_window();
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	native_size(swap_chain_window, &desc.Width, &desc.Height);
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.Scaling = DXGI_SCALING_STRETCH;
	if (FAILED(factory->CreateSwapChainForCoreWindow(device.Get(), core_window, &desc, nullptr,
													 &swap_chain))) {
		return false;
	}
	// No SetRotation: the phone's compositor shows the buffer as is on the panel
	// and would turn it back, so the frame is turned here alone (see gles_present)

	target_w = width;
	target_h = height;
	if (!create_target() || !create_pipeline()) {
		return false;
	}

	load_identity(matrices[0]);
	load_identity(matrices[1]);
	glViewport(0, 0, width, height);

	// The state the Android port's Java side set before the game started
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	return true;
}

void gles_set_window(int width, int height, int rotation) {
	std::lock_guard<std::mutex> lock(window_lock);
	wanted_window.width = width > 0 ? width : 1;
	wanted_window.height = height > 0 ? height : 1;
	wanted_window.rotation = rotation;
}

void gles_window_to_frame(float x, float y, float *frame_x, float *frame_y) {
	WindowState w = get_wanted_window();
	float uv[2];
	rotated_uv(is_portrait(w) ? DXGI_MODE_ROTATION_ROTATE270 : DXGI_MODE_ROTATION_IDENTITY,
			   x / w.width, y / w.height, uv);
	*frame_x = uv[0] * target_w;
	*frame_y = uv[1] * target_h;
}

void gles_present() {
	ComPtr<ID3D11Texture2D> back_buffer;
	ComPtr<ID3D11RenderTargetView> back_view;
	D3D11_TEXTURE2D_DESC back_desc;
	DrawState state = {};
	UINT start;

	WindowState window = get_wanted_window();
	if (window != swap_chain_window) {
		UINT width, height;
		native_size(window, &width, &height);
		swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
		swap_chain_window = window;
	}

	swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	back_buffer->GetDesc(&back_desc);
	if (FAILED(device->CreateRenderTargetView(back_buffer.Get(), nullptr, &back_view))) {
		return;
	}

	// The offscreen target drawn over the whole back buffer, turned
	static const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
	scratch.clear();
	for (const float *c : corners) {
		Vertex v = {{c[0] * 2 - 1, 1 - c[1] * 2, 0}, {1, 1, 1, 1}};
		rotated_uv(frame_rotation(swap_chain_window), c[0], c[1], v.uv);
		scratch.push_back(v);
	}
	if (!upload_scratch(&start)) {
		return;
	}
	load_identity(state.mvp);
	state.alpha_ref = -1.0f;
	set_draw_state(state);

	bool saved_blend = blend, saved_depth = depth_test, saved_cull = cull_face;
	blend = depth_test = cull_face = false;
	Texture nearest;
	nearest.min_filter = nearest.mag_filter = GL_NEAREST;
	nearest.wrap_s = nearest.wrap_t = GL_CLAMP_TO_EDGE;
	ID3D11SamplerState *sampler = get_sampler(nearest);
	D3D11_VIEWPORT vp = {0, 0, (float) back_desc.Width, (float) back_desc.Height, 0, 1};

	UINT stride = sizeof(Vertex), offset = 0;

	// Unbind the target as output first, or Direct3D refuses it as the input
	context->OMSetRenderTargets(1, back_view.GetAddressOf(), nullptr);
	context->IASetInputLayout(input_layout.Get());
	context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->VSSetConstantBuffers(0, 1, state_buffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, state_buffer.GetAddressOf());
	context->PSSetShader(texture_shader.Get(), nullptr, 0);
	context->PSSetShaderResources(0, 1, target_srv.GetAddressOf());
	context->PSSetSamplers(0, 1, &sampler);
	context->RSSetState(get_raster_state());
	context->RSSetViewports(1, &vp);
	context->OMSetBlendState(get_blend_state(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(get_depth_state(), 0);
	context->Draw(6, start);
	blend = saved_blend;
	depth_test = saved_depth;
	cull_face = saved_cull;

	// The target goes back to being drawn into
	ID3D11ShaderResourceView *no_srv = nullptr;
	context->PSSetShaderResources(0, 1, &no_srv);
	swap_chain->Present(1, 0);
}

void gles_trim() {
	ComPtr<IDXGIDevice3> dxgi_device;
	if (SUCCEEDED(device.As(&dxgi_device))) {
		context->ClearState();
		dxgi_device->Trim();
	}
}

void gles_get_size(int *width, int *height) {
	*width = target_w;
	*height = target_h;
}

extern "C" {

void glAlphaFunc(GLenum func, GLclampf ref) {
	// The game only uses GL_GEQUAL
	alpha_ref = ref;
}

void glBindTexture(GLenum target, GLuint texture) {
	bound_texture = texture;
	if (texture) {
		textures[texture];
	}
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
	blend_src = sfactor;
	blend_dst = dfactor;
}

void glClear(GLbitfield mask) {
	if (mask & GL_COLOR_BUFFER_BIT) {
		context->ClearRenderTargetView(target_view.Get(), clear_color);
	}
	if (mask & GL_DEPTH_BUFFER_BIT) {
		context->ClearDepthStencilView(depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	}
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
	clear_color[0] = red;
	clear_color[1] = green;
	clear_color[2] = blue;
	clear_color[3] = alpha;
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
	current_color[0] = red;
	current_color[1] = green;
	current_color[2] = blue;
	current_color[3] = alpha;
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha) {
	glColor4f(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
	color_array.size = size;
	color_array.type = type;
	color_array.stride = stride;
	color_array.pointer = (const GLubyte *) pointer;
}

void glCopyTexImage2D(GLenum target_, GLint level, GLenum internalformat, GLint x, GLint y,
					  GLsizei width, GLsizei height, GLint border) {
	Texture *t = find_texture(bound_texture);
	if (!t || !create_texture(*t, width, height, nullptr)) {
		return;
	}
	D3D11_BOX box = target_box(x, y, width, height);
	context->CopySubresourceRegion(t->tex.Get(), 0, 0, 0, 0, target.Get(), 0, &box);
	t->flip_v = true;
}

void glCullFace(GLenum mode) {
	cull_mode = mode;
}

void glDeleteTextures(GLsizei n, const GLuint *ids) {
	for (GLsizei i = 0; i < n; ++i) {
		if (ids[i]) {
			textures.erase(ids[i]);
			if (bound_texture == ids[i]) {
				bound_texture = 0;
			}
		}
	}
}

void glDepthFunc(GLenum func) {
	depth_func = func;
}

static void set_cap(GLenum cap, bool value) {
	switch (cap) {
		case GL_TEXTURE_2D: texture_2d = value; break;
		case GL_ALPHA_TEST: alpha_test = value; break;
		case GL_DEPTH_TEST: depth_test = value; break;
		case GL_BLEND: blend = value; break;
		case GL_CULL_FACE: cull_face = value; break;
	}
}

void glDisable(GLenum cap) {
	set_cap(cap, false);
}

void glEnable(GLenum cap) {
	set_cap(cap, true);
}

static ClientArray *client_array(GLenum array) {
	switch (array) {
		case GL_VERTEX_ARRAY: return &vertex_array;
		case GL_COLOR_ARRAY: return &color_array;
		case GL_TEXTURE_COORD_ARRAY: return &texcoord_array;
		default: return nullptr;
	}
}

void glDisableClientState(GLenum array) {
	if (ClientArray *a = client_array(array)) {
		a->enabled = false;
	}
}

void glEnableClientState(GLenum array) {
	if (ClientArray *a = client_array(array)) {
		a->enabled = true;
	}
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
	D3D11_PRIMITIVE_TOPOLOGY topology;
	DrawState state = {};
	Texture *tex;
	UINT start;

	if (!vertex_array.enabled || count <= 0) {
		return;
	}

	// Direct3D 11 has no triangle fans, so they become lists
	scratch.clear();
	if (mode == GL_TRIANGLE_FAN) {
		topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		for (GLint i = 1; i + 1 < count; ++i) {
			scratch.push_back(fetch_vertex(first));
			scratch.push_back(fetch_vertex(first + i));
			scratch.push_back(fetch_vertex(first + i + 1));
		}
	} else {
		topology = mode == GL_LINES ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST :
				   D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		for (GLint i = 0; i < count; ++i) {
			scratch.push_back(fetch_vertex(first + i));
		}
	}
	if (!upload_scratch(&start)) {
		return;
	}

	tex = texture_2d ? find_texture(bound_texture) : nullptr;
	if (tex && !tex->srv) {
		tex = nullptr;
	}

	multiply(state.mvp, matrices[GL_PROJECTION - GL_MODELVIEW], matrices[0]);
	state.alpha_ref = alpha_test ? alpha_ref : -1.0f;
	state.flip_v = tex && tex->flip_v ? 1.0f : 0.0f;
	set_draw_state(state);

	UINT stride = sizeof(Vertex), offset = 0;
	context->IASetInputLayout(input_layout.Get());
	context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
	context->IASetPrimitiveTopology(topology);
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->VSSetConstantBuffers(0, 1, state_buffer.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, state_buffer.GetAddressOf());
	if (tex) {
		ID3D11SamplerState *sampler = get_sampler(*tex);
		context->PSSetShader(texture_shader.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, tex->srv.GetAddressOf());
		context->PSSetSamplers(0, 1, &sampler);
	} else {
		context->PSSetShader(color_shader.Get(), nullptr, 0);
	}
	context->RSSetState(get_raster_state());
	context->RSSetViewports(1, &viewport);
	context->OMSetBlendState(get_blend_state(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(get_depth_state(), 0);
	context->OMSetRenderTargets(1, target_view.GetAddressOf(), depth_view.Get());
	context->Draw((UINT) scratch.size(), start);
}

void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
	float m[16] = {};
	m[0] = 2 * n / (r - l);
	m[5] = 2 * n / (t - b);
	m[8] = (r + l) / (r - l);
	m[9] = (t + b) / (t - b);
	m[10] = -(f + n) / (f - n);
	m[11] = -1;
	m[14] = -2 * f * n / (f - n);
	multiply_current(m);
}

void glGenTextures(GLsizei n, GLuint *ids) {
	for (GLsizei i = 0; i < n; ++i) {
		ids[i] = next_texture++;
		textures[ids[i]];
	}
}

void glHint(GLenum target_, GLenum mode) {
}

GLboolean glIsTexture(GLuint texture) {
	return texture && textures.count(texture) ? GL_TRUE : GL_FALSE;
}

void glLoadIdentity(void) {
	load_identity(matrices[matrix_mode]);
}

void glMatrixMode(GLenum mode) {
	matrix_mode = mode == GL_PROJECTION ? 1 : 0;
}

void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
	float m[16] = {};
	m[0] = 2 / (r - l);
	m[5] = 2 / (t - b);
	m[10] = -2 / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1;
	multiply_current(m);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
				  GLvoid *pixels) {
	D3D11_TEXTURE2D_DESC desc = {};
	ComPtr<ID3D11Texture2D> staging;
	D3D11_MAPPED_SUBRESOURCE mapped;

	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
		return;
	}
	D3D11_BOX box = target_box(x, y, width, height);
	context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, target.Get(), 0, &box);
	if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
		return;
	}
	// GL returns the bottom row first
	for (GLsizei row = 0; row < height; ++row) {
		memcpy((GLubyte *) pixels + row * width * 4,
			   (const GLubyte *) mapped.pData + (height - 1 - row) * mapped.RowPitch, width * 4);
	}
	context->Unmap(staging.Get(), 0);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
	texcoord_array.size = size;
	texcoord_array.type = type;
	texcoord_array.stride = stride;
	texcoord_array.pointer = (const GLubyte *) pointer;
}

void glTexImage2D(GLenum target_, GLint level, GLint internalformat, GLsizei width,
				  GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
	Texture *t = find_texture(bound_texture);
	if (t && level == 0) {
		create_texture(*t, width, height, pixels);
	}
}

void glTexParameteri(GLenum target_, GLenum pname, GLint param) {
	Texture *t = find_texture(bound_texture);
	if (!t) {
		return;
	}
	switch (pname) {
		case GL_TEXTURE_MIN_FILTER: t->min_filter = param; break;
		case GL_TEXTURE_MAG_FILTER: t->mag_filter = param; break;
		case GL_TEXTURE_WRAP_S: t->wrap_s = param; break;
		case GL_TEXTURE_WRAP_T: t->wrap_t = param; break;
	}
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
	vertex_array.size = size;
	vertex_array.type = type;
	vertex_array.stride = stride;
	vertex_array.pointer = (const GLubyte *) pointer;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
	// GL counts y from the bottom of the window
	viewport.TopLeftX = (float) x;
	viewport.TopLeftY = (float) (target_h - y - height);
	viewport.Width = (float) width;
	viewport.Height = (float) height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
}

}
