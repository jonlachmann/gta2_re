// Port (docs/port.md): COM declarations for the DirectX headers the game includes. Interfaces are plain
// C++ classes with virtual methods, which have the same layout as COM vtables on 32-bit x86.
#pragma once
#include "windows.h"

#define interface struct
#define STDMETHODCALLTYPE WINAPI
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) struct iface
#define DECLARE_INTERFACE_(iface, base) struct iface : public base
#define MIDL_INTERFACE(x) struct
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C" const GUID name __attribute__((weak)) = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

DECLARE_INTERFACE(IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};
typedef IUnknown* LPUNKNOWN;

inline HRESULT CoInitialize(LPVOID)
{
    return S_OK;
}
inline void CoUninitialize()
{
}
