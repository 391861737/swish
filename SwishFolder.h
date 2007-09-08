// SwishFolder.h : Declaration of the CSwishFolder

#ifndef SWISHFOLDER_H
#define SWISHFOLDER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "stdafx.h"
#include "resource.h"       // main symbols

#include "PidlManager.h"

#define _ATL_DEBUG_QI

// ISwishFolder
[
	object,
	uuid("b816a839-5022-11dc-9153-0090f5284f85"),
	helpstring("ISwishFolder Interface"),
	pointer_default(unique)
]
__interface ISwishFolder : IUnknown
{
};


// CSwishFolder
[
	coclass,
	default(ISwishFolder),
	threading(apartment),
	vi_progid("Swish.SwishFolder"),
	progid("Swish.SwishFolder.1"),
	version(1.0),
	registration_script("SwishFolder.rgs"),
	uuid("b816a83a-5022-11dc-9153-0090f5284f85"),
	helpstring("SwishFolder Class")
]
class ATL_NO_VTABLE CSwishFolder :
	public ISwishFolder,
	public IShellFolder,
    public IPersistFolder,
	public IExtractIcon
{
public:
	CSwishFolder() : m_pidlRoot(NULL)
	{
	}

	DECLARE_PROTECT_FINAL_CONSTRUCT()
	HRESULT FinalConstruct()
	{
		return S_OK;
	}
	void FinalRelease()
	{
	}

	// Init function - call right after constructing a CSwishFolder object
    HRESULT _init ( CSwishFolder* pParentFolder, LPCITEMIDLIST pidl )
    {
        m_pParentFolder = pParentFolder;

        if ( NULL != m_pParentFolder )
            m_pParentFolder->AddRef();

        m_pidl = m_PidlManager.Copy ( pidl );

        return S_OK;
    }

    // IPersist
    STDMETHOD(GetClassID)( CLSID* );

	// IPersistFolder
    STDMETHOD(Initialize)( LPCITEMIDLIST );

	// IShellFolder
    STDMETHOD(BindToObject)( LPCITEMIDLIST, LPBC, REFIID, void** );
	STDMETHOD(EnumObjects)( HWND, DWORD, LPENUMIDLIST* );
    STDMETHOD(CreateViewObject)( HWND, REFIID, void** );
    STDMETHOD(GetAttributesOf) ( UINT, LPCITEMIDLIST*, LPDWORD );
    STDMETHOD(GetUIObjectOf)
		( HWND, UINT, LPCITEMIDLIST*, REFIID, LPUINT, void** );
	STDMETHOD(CompareIDs)
		( LPARAM, LPCITEMIDLIST, LPCITEMIDLIST );


    STDMETHOD(BindToStorage)( LPCITEMIDLIST, LPBC, REFIID, void** )
        { return E_NOTIMPL; }
    STDMETHOD(GetDisplayNameOf)( PCUITEMID_CHILD, SHGDNF, LPSTRRET );
    STDMETHOD(ParseDisplayName)
		( HWND, LPBC, LPOLESTR, LPDWORD, LPITEMIDLIST*, LPDWORD )
        { return E_NOTIMPL; }
    STDMETHOD(SetNameOf)( HWND, LPCITEMIDLIST, LPCOLESTR, DWORD, LPITEMIDLIST* )
        { return E_NOTIMPL; }

	// IExtractIcon
	STDMETHOD(Extract)( LPCTSTR pszFile, UINT nIconIndex, HICON *phiconLarge, 
						HICON *phiconSmall, UINT nIconSize );
	STDMETHOD(GetIconLocation)( UINT uFlags, LPTSTR szIconFile, UINT cchMax, 
								int *piIndex, UINT *pwFlags );

private:
    CPidlManager       m_PidlManager;
	LPCITEMIDLIST      m_pidlRoot;
    CSwishFolder*      m_pParentFolder;
    LPITEMIDLIST       m_pidl;
	std::vector<HOSTPIDL> m_vecConnData;

	CString GetLongNameFromPIDL( PCUITEMID_CHILD pidl, BOOL fCanonical );
	CString GetLabelFromPIDL( PCUITEMID_CHILD pidl );

};

#endif // SWISHFOLDER_H
