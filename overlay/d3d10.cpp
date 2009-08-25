/* Copyright (C) 2005-2009, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "lib.h"
#include <d3d10.h>
#include <d3dx10.h>

DXGIData *dxgi;

static char fx[8192];
static int fxlen = 0;
static bool bHooked = false;
static bool bChaining = false;
static HardHook hhPresent;
static HardHook hhResize;

typedef HRESULT(__stdcall *CreateDXGIFactoryType)(REFIID, void **);
typedef HRESULT(__stdcall *D3D10CreateDeviceAndSwapChainType)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device **);

typedef HRESULT(__stdcall *D3D10CreateStateBlockType)(ID3D10Device *, D3D10_STATE_BLOCK_MASK *, ID3D10StateBlock **);
typedef HRESULT(__stdcall *D3D10StateBlockMaskEnableAllType)(D3D10_STATE_BLOCK_MASK *);
typedef HRESULT(__stdcall *D3D10CreateEffectFromMemoryType)(void *, SIZE_T, UINT, ID3D10Device *, ID3D10EffectPool *, ID3D10Effect **);

typedef HRESULT(__stdcall *PresentType)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(__stdcall *ResizeBuffersType)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);

#define HMODREF(mod, func) func##Type p##func = (func##Type) GetProcAddress(mod, #func)

struct SimpleVertex
{
    D3DXVECTOR3 Pos;
};

struct D10State {
	ID3D10Device *pDevice;
	ID3D10StateBlock *pOrigStateBlock;
	ID3D10StateBlock *pMyStateBlock;
	ID3D10RenderTargetView *pRTV;
	ID3D10Effect *pEffect;
	ID3D10EffectTechnique *pTechnique;
	ID3D10InputLayout *pVertexLayout;
	ID3D10Buffer *pVertexBuffer;
	ID3D10BlendState *pBlendState;

	D10State(IDXGISwapChain *, ID3D10Device *);
	~D10State();
	void draw();
};

map<IDXGISwapChain *, D10State *> states;

D10State::D10State(IDXGISwapChain *pSwapChain, ID3D10Device *pDevice) {
	static HMODREF(GetModuleHandleW(L"D3D10.DLL"), D3D10CreateEffectFromMemory);
	static HMODREF(GetModuleHandleW(L"D3D10.DLL"), D3D10CreateStateBlock);
	static HMODREF(GetModuleHandleW(L"D3D10.DLL"), D3D10StateBlockMaskEnableAll);

	HRESULT hr;

	this->pDevice = pDevice;

	D3D10_STATE_BLOCK_MASK StateBlockMask;
	ZeroMemory(&StateBlockMask, sizeof(StateBlockMask));
	pD3D10StateBlockMaskEnableAll(&StateBlockMask);
	pD3D10CreateStateBlock(pDevice, &StateBlockMask, &pOrigStateBlock);
	pD3D10CreateStateBlock(pDevice, &StateBlockMask, &pMyStateBlock);

	pOrigStateBlock->Capture();

	ID3D10Texture2D* pBackBuffer = NULL;
	hr = pSwapChain->GetBuffer( 0, __uuidof( *pBackBuffer ), ( LPVOID* )&pBackBuffer );

	pDevice->ClearState();

	D3D10_TEXTURE2D_DESC backBufferSurfaceDesc;
	pBackBuffer->GetDesc( &backBufferSurfaceDesc );

	D3D10_VIEWPORT vp;
	vp.Width = backBufferSurfaceDesc.Width;
	vp.Height = backBufferSurfaceDesc.Height;
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	pDevice->RSSetViewports( 1, &vp );

	hr = pDevice->CreateRenderTargetView( pBackBuffer, NULL, &pRTV );

	pDevice->OMSetRenderTargets( 1, &pRTV, NULL );

	D3D10_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.BlendEnable[0] = TRUE;
	blend.SrcBlend = D3D10_BLEND_SRC_ALPHA;
	blend.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
	blend.BlendOp = D3D10_BLEND_OP_ADD;
	blend.SrcBlendAlpha = D3D10_BLEND_SRC_ALPHA;
	blend.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
	blend.BlendOpAlpha = D3D10_BLEND_OP_ADD;
	blend.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;

	pDevice->CreateBlendState(&blend, &pBlendState);
	float bf[4];
	pDevice->OMSetBlendState(pBlendState, bf, 0xffffffff);

/*
	DWORD dwShaderFlags = D3D10_SHADER_ENABLE_STRICTNESS | D3D10_SHADER_DEBUG;
	hr = D3DX10CreateEffectFromFileW( L"C:\\dev\\dxsdk\\Samples\\C++\\Direct3D10\\Tutorials\\Tutorial02\\Tutorial02.fx", NULL, NULL, "fx_4_0", dwShaderFlags, 0,
						 pDevice, NULL, NULL, &pEffect, NULL, NULL );
*/
	pEffect = NULL;
	ods("Effectum maximum %p %d", fx, fxlen);
	pD3D10CreateEffectFromMemory(fx, fxlen, 0, pDevice, NULL, &pEffect);

	pTechnique = pEffect->GetTechniqueByName( "Render" );

	// Define the input layout
	D3D10_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = sizeof( layout ) / sizeof( layout[0] );

	// Create the input layout
	D3D10_PASS_DESC PassDesc;
	pTechnique->GetPassByIndex( 0 )->GetDesc( &PassDesc );
	hr = pDevice->CreateInputLayout( layout, numElements, PassDesc.pIAInputSignature, PassDesc.IAInputSignatureSize, &pVertexLayout );
	pDevice->IASetInputLayout( pVertexLayout );

	// Create vertex buffer
	SimpleVertex vertices[] =
	{
		D3DXVECTOR3( 0.0f, 0.9f, 0.5f ),
		D3DXVECTOR3( 0.9f, -0.9f, 0.5f ),
		D3DXVECTOR3( -0.9f, -0.9f, 0.5f ),
	};
	D3D10_BUFFER_DESC bd;
	bd.Usage = D3D10_USAGE_DEFAULT;
	bd.ByteWidth = sizeof( SimpleVertex ) * 3;
	bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = 0;
	D3D10_SUBRESOURCE_DATA InitData;
	InitData.pSysMem = vertices;
	hr = pDevice->CreateBuffer( &bd, &InitData, &pVertexBuffer );

	// Set vertex buffer
	UINT stride = sizeof( SimpleVertex );
	UINT offset = 0;
	pDevice->IASetVertexBuffers( 0, 1, &pVertexBuffer, &stride, &offset );

	// Set primitive topology
	pDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	pMyStateBlock->Capture();
	pOrigStateBlock->Apply();

	pBackBuffer->Release();
}

D10State::~D10State() {
	pBlendState->Release();
	pVertexBuffer->Release();
	pVertexLayout->Release();
	pEffect->Release();
	pRTV->Release();

	pMyStateBlock->ReleaseAllDeviceObjects();
	pMyStateBlock->Release();

	pOrigStateBlock->ReleaseAllDeviceObjects();
	pOrigStateBlock->Release();
}

void D10State::draw() {
	pOrigStateBlock->Capture();
	pMyStateBlock->Apply();

	// Render a triangle
	D3D10_TECHNIQUE_DESC techDesc;
	pTechnique->GetDesc( &techDesc );
	for( UINT p = 0; p < techDesc.Passes; ++p )
	{
//		ods("Pass %d", p);
		pTechnique->GetPassByIndex( p )->Apply( 0 );
		pDevice->Draw( 3, 0 );
	}
	pOrigStateBlock->Apply();
}



static HRESULT __stdcall myPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
	HRESULT hr;
//	ods("DXGI: Device Present");

	ID3D10Device *pDevice = NULL;
	hr = pSwapChain->GetDevice(__uuidof(ID3D10Device), (void **) &pDevice);
	if (pDevice) {
		D10State *ds = states[pSwapChain];
		if (ds && ds->pDevice != pDevice) {
			ods("DXGI: SwapChain device changed");
			delete ds;
			ds = NULL;
		}
		if (! ds) {
			ds = new D10State(pSwapChain, pDevice);
			states[pSwapChain] = ds;
		}

		ds->draw();

		pDevice->Release();
	}

	PresentType oPresent = (PresentType) hhPresent.call;
	hhPresent.restore();
	hr = oPresent(pSwapChain, SyncInterval, Flags);
	hhPresent.inject();
	return hr;
}

static HRESULT __stdcall myResize(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
	HRESULT hr;

	D10State *ds = states[pSwapChain];
	if (ds) {
		delete ds;
		states.erase(pSwapChain);
	}

	ResizeBuffersType oResize = (ResizeBuffersType) hhResize.call;
	hhResize.restore();
	hr = oResize(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
	hhResize.inject();
	return hr;
}


static void HookPresentRaw(voidFunc vfPresent) {
	ods("DXGI: Injecting Present Raw");
	hhPresent.setup(vfPresent, reinterpret_cast<voidFunc>(myPresent));
}

static void HookResizeRaw(voidFunc vfResize) {
	ods("DXGI: Injecting ResizeBuffers Raw");
	hhResize.setup(vfResize, reinterpret_cast<voidFunc>(myResize));
}

void checkDXGIHook(bool preonly) {
	if (bChaining) {
		return;
		ods("DXGI: Causing a chain");
	}

	bChaining = true;

	HMODULE hDXGI = GetModuleHandleW(L"DXGI.DLL");

	if (hDXGI != NULL) {
		if (! bHooked) {
			wchar_t procname[2048];
			GetModuleFileNameW(NULL, procname, 2048);
			fods("DXGI: Hookcheck '%ls'", procname);
			bHooked = true;

			// Add a ref to ourselves; we do NOT want to get unloaded directly from this process.
			GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<char *>(&checkDXGIHook), &hSelf);

			// Can we use the prepatch data?
			GetModuleFileNameW(hDXGI, procname, 2048);
			if (_wcsicmp(dxgi->wcFileName, procname) == 0) {
				unsigned char *raw = (unsigned char *) hDXGI;
				HookPresentRaw((voidFunc)(raw + dxgi->iOffsetPresent));
				HookResizeRaw((voidFunc)(raw + dxgi->iOffsetResize));

				FILE *f = fopen("C:\\dev\\mumble\\overlay\\overlay.fxo", "rb");
				fxlen=fread(fx, 1, 8192, f);
				ods("Gotcha %p %d", f, fxlen);
				fclose(f);

			} else if (! preonly) {
				fods("DXGI Interface changed, can't rawpatch");
			} else {
				bHooked = false;
			}
		}
	}

	bChaining = false;
}

extern "C" __declspec(dllexport) void __cdecl PrepareDXGI() {
	ods("Preparing static data for DXGI Injection");

	HMODULE hD3D10 = LoadLibrary("D3D10.DLL");
	HMODULE hDXGI = LoadLibrary("DXGI.DLL");
	HRESULT hr;

	dxgi->wcFileName[0] = 0;
	dxgi->iOffsetPresent = 0;

	if (hDXGI != NULL && hD3D10 != NULL) {
		CreateDXGIFactoryType pCreateDXGIFactory = reinterpret_cast<CreateDXGIFactoryType>(GetProcAddress(hDXGI, "CreateDXGIFactory"));
		ods("Got %p", pCreateDXGIFactory);
		if (pCreateDXGIFactory) {
			IDXGIFactory * pFactory;
			hr = pCreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory) );
			if (pFactory) {
				HWND hwnd = CreateWindowW( L"STATIC", L"Mumble DXGI Window", WS_OVERLAPPEDWINDOW,
										  CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0,
										  NULL, NULL, 0 );

				IDXGIAdapter *pAdapter = NULL;
				pFactory->EnumAdapters(0, &pAdapter);

				D3D10CreateDeviceAndSwapChainType pD3D10CreateDeviceAndSwapChain = reinterpret_cast<D3D10CreateDeviceAndSwapChainType>(GetProcAddress(hD3D10, "D3D10CreateDeviceAndSwapChain"));

				IDXGISwapChain *pSwapChain = NULL;
				ID3D10Device *pDevice = NULL;

				DXGI_SWAP_CHAIN_DESC desc;
				ZeroMemory(&desc, sizeof(desc));

		        RECT rcWnd;
				GetClientRect(hwnd, &rcWnd );
				desc.BufferDesc.Width = rcWnd.right - rcWnd.left;
				desc.BufferDesc.Height = rcWnd.bottom - rcWnd.top;

				ods("W %d H %d", desc.BufferDesc.Width, desc.BufferDesc.Height);

				desc.BufferDesc.RefreshRate.Numerator = 60;
				desc.BufferDesc.RefreshRate.Denominator = 1;
				desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
				desc.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;

				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;

				desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

				desc.BufferCount = 2;

				desc.OutputWindow = hwnd;

				desc.Windowed = true;

				desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

				hr = pD3D10CreateDeviceAndSwapChain(pAdapter, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &desc, &pSwapChain, &pDevice);

				if (pDevice && pSwapChain) {
						HMODULE hRef;
						void ***vtbl = (void ***) pSwapChain;
						void *pPresent = (*vtbl)[8];
						if (! GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (char *) pPresent, &hRef)) {
							ods("DXGI: Failed to get module for Present");
						} else {
							GetModuleFileNameW(hRef, dxgi->wcFileName, 2048);
							unsigned char *b = (unsigned char *) pPresent;
							unsigned char *a = (unsigned char *) hRef;
							dxgi->iOffsetPresent = b-a;
							ods("DXGI: Successfully found Present offset: %ls: %d", dxgi->wcFileName, dxgi->iOffsetPresent);
						}

						void *pResize = (*vtbl)[13];
						if (! GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (char *) pResize, &hRef)) {
							ods("DXGI: Failed to get module for ReiszeBuffers");
						} else {
							wchar_t buff[2048];
							GetModuleFileNameW(hRef, buff, 2048);
							if (wcscmp(buff, dxgi->wcFileName) == 0) {
								unsigned char *b = (unsigned char *) pResize;
								unsigned char *a = (unsigned char *) hRef;
								dxgi->iOffsetResize = b-a;
								ods("DXGI: Successfully found ResizeBuffers offset: %ls: %d", dxgi->wcFileName, dxgi->iOffsetPresent);
							}
						}
				}
				if (pDevice)
					pDevice->Release();
				if (pSwapChain)
					pSwapChain->Release();
				DestroyWindow(hwnd);

				pFactory->Release();
			}
		}
	}
}
