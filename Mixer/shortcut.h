#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <ShObjIdl_core.h>
#include <atlstr.h>

#define CHECK( hr ) { const HRESULT __hr = ( hr ); if( FAILED( __hr ) ) return __hr; }

HRESULT resolveShortcutTarget(HWND wnd, const CString& lnk, CString& target)
{
	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize has already been called. 
	CComPtr<IShellLink> psl;
	CHECK(psl.CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER));

	// Get a pointer to the IPersistFile interface. 
	CComPtr<IPersistFile> ppf;
	CHECK(psl->QueryInterface(IID_PPV_ARGS(&ppf)));

	WCHAR wsz[MAX_PATH];

	MultiByteToWideChar(CP_ACP, 0, lnk, -1, wsz, MAX_PATH);

	// Load the shortcut. 
	CHECK(ppf->Load(wsz, STGM_READ));

	// Resolve the link. 
	CHECK(psl->Resolve(wnd, 0));

	// Get the path to the link target. 
	const HRESULT hr = psl->GetPath(target.GetBufferSetLength(MAX_PATH), MAX_PATH, nullptr, 0);
	target.ReleaseBuffer();
	return hr;
}
