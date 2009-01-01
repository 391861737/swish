/*  Dummy IShellFolder namespace extension to test CFolder abstract class.

    Copyright (C) 2008  Alexander Lamaison <awl03@doc.ic.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once
#include "resource.h"       // main symbols

#include <CoFactory.h>
#include <Folder.h>

#include <pshpack1.h>
struct DummyItemId
{
	USHORT cb;
	DWORD dwFingerprint;
	int level;

	static const DWORD FINGERPRINT = 0x624a0fe5;
};
#include <poppack.h>

/**
 * Copy-policy to manage copying and destruction of dummy pidls.
 */
struct _CopyPidl
{
	static HRESULT copy(PITEMID_CHILD *ppidlCopy, const PITEMID_CHILD *ppidl)
	{
		ATLASSERT(ppidl);
		ATLASSERT(ppidlCopy);

		if (*ppidl == NULL)
		{
			*ppidlCopy = NULL;
			return S_OK;
		}

		*ppidlCopy = ::ILCloneChild(*ppidl);
		if (*ppidlCopy)
			return S_OK;
		else
			return E_OUTOFMEMORY;
	}

	static void init(PITEMID_CHILD* /* ppidl */) { }
	static void destroy(PITEMID_CHILD *ppidl)
	{
		::ILFree(*ppidl);
	}
};

class ATL_NO_VTABLE CDummyFolder :
	public CFolder,
	private CCoFactory<CDummyFolder>
{
public:

	BEGIN_COM_MAP(CDummyFolder)
		COM_INTERFACE_ENTRY(IShellFolder)
		COM_INTERFACE_ENTRY_CHAIN(CFolder)
	END_COM_MAP()
	
	/**
	 * Create instance of the CDummyFolder class and return IShellFolder.
	 *
	 * @returns Smart pointer to the CDummyFolder's IShellFolder interface.
	 * @throws  CAtlException if creation fails.
	 */
	static CComPtr<IShellFolder> Create()
	throw(...)
	{
		CComPtr<CDummyFolder> spObject = spObject->CreateCoObject();
		return spObject.p;
	}

protected:

	__override void ValidatePidl(PCUIDLIST_RELATIVE pidl) const throw(...);
	__override CLSID GetCLSID() const;
	__override CComPtr<IShellFolder> CreateSubfolder(
		PCIDLIST_ABSOLUTE pidlRoot)
		const throw(...);
	__override int ComparePIDLs(
		__in PCUIDLIST_RELATIVE pidl1, __in PCUIDLIST_RELATIVE pidl2,
		USHORT uColumn, bool fCompareAllFields, bool fCanonical)
		const throw(...);

public:

	CDummyFolder();
	~CDummyFolder();

	DECLARE_PROTECT_FINAL_CONSTRUCT()
	HRESULT FinalConstruct();
	void FinalRelease();

	__override STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl);

public: // IShellFolder methods

	IFACEMETHODIMP ParseDisplayName(
		__in_opt HWND hwnd,
		__in_opt IBindCtx *pbc,
		__in PWSTR pwszDisplayName,
		__reserved  ULONG *pchEaten,
		__deref_out_opt PIDLIST_RELATIVE *ppidl,
		__inout_opt ULONG *pdwAttributes);

	IFACEMETHODIMP EnumObjects(
		__in_opt HWND hwnd,
		SHCONTF grfFlags,
		__deref_out_opt IEnumIDList **ppenumIDList);

	IFACEMETHODIMP GetAttributesOf( 
		UINT cidl,
		__in_ecount_opt(cidl) PCUITEMID_CHILD_ARRAY apidl,
		__inout SFGAOF *rgfInOut);

	IFACEMETHODIMP GetUIObjectOf( 
		__in_opt HWND hwndOwner,
		UINT cidl,
		__in_ecount_opt(cidl) PCUITEMID_CHILD_ARRAY apidl,
		__in REFIID riid,
		__reserved  UINT *rgfReserved,
		__deref_out_opt void **ppv);

	IFACEMETHODIMP GetDisplayNameOf( 
		__in PCUITEMID_CHILD pidl,
		SHGDNF uFlags,
		__out STRRET *pName);

	IFACEMETHODIMP SetNameOf( 
		__in_opt HWND hwnd,
		__in PCUITEMID_CHILD pidl,
		__in PCWSTR pszName,
		SHGDNF uFlags,
		__deref_out_opt PITEMID_CHILD *ppidlOut);

public: // IShellFolder2 methods

	IFACEMETHODIMP GetDefaultColumn(
		DWORD dwRes,
		__out ULONG *pSort,
		__out ULONG *pDisplay);

	IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, __out SHCOLSTATEF *pcsFlags);

	IFACEMETHODIMP GetDetailsEx(
		__in PCUITEMID_CHILD pidl,
		__in const SHCOLUMNID *pscid,
		__out VARIANT *pv);

	IFACEMETHODIMP GetDetailsOf(
		__in_opt PCUITEMID_CHILD pidl,
		UINT iColumn,
		__out SHELLDETAILS *psd);

	IFACEMETHODIMP MapColumnToSCID(
		UINT iColumn,
		__out SHCOLUMNID *pscid);

private:
	PITEMID_CHILD m_apidl[1];

	/**
	 * Static dispatcher for Default Context Menu callback
	 */
	static HRESULT __callback CALLBACK MenuCallback(
		__in_opt IShellFolder *psf, HWND hwnd, __in_opt IDataObject *pdtobj, 
		UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		ATLENSURE_RETURN(psf);
		return static_cast<CDummyFolder *>(psf)->OnMenuCallback(
			hwnd, pdtobj, uMsg, wParam, lParam);
	}

	/** @name Default context menu event handlers */
	// @{
	HRESULT OnMenuCallback( HWND hwnd, IDataObject *pdtobj, 
		UINT uMsg, WPARAM wParam, LPARAM lParam );
	HRESULT OnMergeContextMenu(
		HWND hwnd, IDataObject *pDataObj, UINT uFlags, QCMINFO& info );
	HRESULT OnInvokeCommand(
		HWND hwnd, IDataObject *pDataObj, int idCmd, PCTSTR pszArgs );
	HRESULT OnInvokeCommandEx(
		HWND hwnd, IDataObject *pDataObj, int idCmd, PDFMICS pdfmics );
	// @}

	HRESULT _GetAssocRegistryKeys( 
		__out UINT *pcKeys, __deref_out_ecount(pcKeys) HKEY **paKeys);

};
