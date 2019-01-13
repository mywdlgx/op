#include "stdafx.h"
#include "DX9hook.h"
#include <d3d9.h>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/named_mutex.hpp> 
#include <exception>
#include "3rd_party/kiero/kiero.h"

char g_shared_res_name[256];
char g_mutex_name[256];
int g_need_hook = 0;
boost::interprocess::named_mutex *g_pmutex = nullptr;

typedef long(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
EndScene g_oEndScene = nullptr;

HRESULT ScreenShot(LPDIRECT3DDEVICE9 pDevice) {
	//save bmp
	HRESULT hr = NULL;
	IDirect3DSurface9 *pSurface;
	hr = pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pSurface);
	if (FAILED(hr)) return hr;

	D3DSURFACE_DESC surface_Desc;
	hr = pSurface->GetDesc(&surface_Desc);
	if (FAILED(hr)) return 0;

	IDirect3DTexture9 *pTex = NULL;
	IDirect3DSurface9 *pTexSurface = NULL;
	pDevice->CreateTexture(surface_Desc.Width,
		surface_Desc.Height,
		1,
		0,
		surface_Desc.Format,
		D3DPOOL_SYSTEMMEM, //必须为这个
		&pTex, NULL);
	if (pTex)
		hr = pTex->GetSurfaceLevel(0, &pTexSurface);
	if (pTexSurface)
		hr = pDevice->GetRenderTargetData(pSurface, pTexSurface);

	D3DLOCKED_RECT lockedRect;

	pTex->LockRect(0, &lockedRect, NULL, D3DLOCK_READONLY);
	/*
		取像素
	*/
	//
	//setlog("step 3. open shared mem name=\"%s\"", g_shared_res_name);
	//Open already created shared memory object.
	try {
		boost::interprocess::shared_memory_object shm(
			boost::interprocess::open_only,
			g_shared_res_name,
			boost::interprocess::read_write);
		//Map the whole shared memory in this process
		
		boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
		auto *p = static_cast<char*>(region.get_address());
		//setlog(L"step 4.try memcpy(p, lockedRect.pBits,size=%d,%d,%d", surface_Desc.Width, surface_Desc.Height, lockedRect.Pitch);
		g_pmutex->lock();
		memcpy(p, (byte*)lockedRect.pBits, lockedRect.Pitch*surface_Desc.Height);
		g_pmutex->unlock();
	}
	catch (std::exception& e) {
		setlog("catch exception:%s", e.what());

	}

	//end
	pTex->UnlockRect(0);

	//D3DXSaveTextureToFile(file, D3DXIFF_BMP, pTex, NULL);
	pSurface->Release();
	//setlog(L"memcpy end.");
	return hr;
}


HRESULT STDMETHODCALLTYPE hkEndScene(IDirect3DDevice9* thiz)
{
	auto ret = g_oEndScene(thiz);
	//每隔段时间调用截图
	static DWORD t = 0;
	if (::GetTickCount() - t > 500) {
		t = ::GetTickCount();
		ScreenShot(thiz);
	}
	return ret;
}

//export function
long SetDX9Hook(HWND hwnd) {

	//setlog(L"step 1. init hook.");
	g_need_hook = 1;
	//initHook(hwnd);
	sprintf(g_shared_res_name, SHARED_RES_NAME_FORMAT, hwnd);
	sprintf(g_mutex_name, MUTEX_NAME_FORMAT, hwnd);
	//step 1. open a mutex.
	try {
		g_pmutex = new boost::interprocess::named_mutex(boost::interprocess::open_only, g_mutex_name);
	}
	catch (std::exception&e) {
		setlog("SetDX9Hook g_mutex_name=%s, std::exception:%s",g_mutex_name, e.what());
	}
	//step 2. init hook
	auto ret = kiero::init(kiero::RenderType::D3D9);
	//step 3. bind function
	if (ret == kiero::Status::Success) {
		kiero::bind(42, (void**)&g_oEndScene, hkEndScene);
		//setlog("bind ok.");
	}
	else {
		setlog("kiero::init false");
	}


	return 1;
}

long UnDX9Hook() {
	g_need_hook = 0;
	if (g_oEndScene) {
		kiero::unbind(42);
	}
	SAFE_DELETE(g_pmutex);
	setlog("un hook");
	return 0;
}