/**
    @file

    SFTP connections Explorer folder implementation.

    @if licence

    Copyright (C) 2007, 2008, 2009  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

    @endif
*/

#include "HostFolder.h"

#include "RemoteFolder.h"
#include "ConnCopyPolicy.h"
#include "ExplorerCallback.h"     // For interaction with Explorer window
#include "Registry.h"             // For saved connection details
#include "host_management.hpp"
#include "swish/catch_com.hpp" // catchCom
#include "swish/debug.hpp"
#include "swish/host_folder/properties.hpp" // property_from_pidl
#include "swish/host_folder/columns.hpp" // property_key_from_column_index
#include "swish/remotelimits.h"   // Text field limits
#include "swish/exception.hpp"    // com_exception
#include "swish/windows_api.hpp" // SHBindToParent
#include "swish/shell_folder/commands/host/host.hpp" // host_folder_commands

#include <winapi/shell/shell.hpp> // strret_to_string

#include <strsafe.h>  // For StringCchCopy

#include <boost/locale.hpp> // translate
#include <boost/shared_ptr.hpp> // shared_ptr
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <cassert> // assert
#include <cstring> // memset
#include <string>

using ATL::CComPtr;
using ATL::CComObject;

using comet::variant_t;

using boost::locale::translate;
using boost::shared_ptr;

using std::wstring;

using swish::host_management::LoadConnectionsFromRegistry;
using swish::exception::com_exception;
using swish::host_folder::property_from_pidl;
using swish::host_folder::property_key_from_column_index;
using swish::shell_folder::commands::host::host_folder_command_provider;

using winapi::shell::pidl::cpidl_t;
using winapi::shell::pidl::apidl_t;
using winapi::shell::pidl::pidl_t;
using winapi::shell::property_key;
using winapi::shell::strret_to_string;

/*--------------------------------------------------------------------------*/
/*                     Remaining IShellFolder functions.                    */
/* Eventually these should be replaced by the internal interfaces of        */
/* CFolder and CSwishFolder.                                                */
/*--------------------------------------------------------------------------*/

/**
 * Create an IEnumIDList which enumerates the items in this folder.
 *
 * @implementing IShellFolder
 *
 * @param[in]  hwndOwner     An optional window handle to be used if 
 *                           enumeration requires user input.
 * @param[in]  grfFlags      Flags specifying which types of items to include 
 *                           in the enumeration. Possible flags are from the 
 *                           @c SHCONT enum.
 * @param[out] ppEnumIDList  Location in which to return the IEnumIDList.
 *
 * @retval S_FALSE if the are no matching items to enumerate.
 */
STDMETHODIMP CHostFolder::EnumObjects(
	HWND hwndOwner, SHCONTF grfFlags, IEnumIDList **ppEnumIDList)
{
	METHOD_TRACE;
	ATLENSURE_RETURN_HR(ppEnumIDList, E_POINTER);
	UNREFERENCED_PARAMETER(hwndOwner); // No UI required to access registry

	HRESULT hr;

    *ppEnumIDList = NULL;

	// This folder only contains folders
	if (!(grfFlags & SHCONTF_FOLDERS) ||
		(grfFlags & (SHCONTF_NETPRINTERSRCH | SHCONTF_SHAREABLE)))
		return S_FALSE;

	try
	{
		// Load connections from HKCU\Software\Swish\Connections
		m_vecConnData = LoadConnectionsFromRegistry();
	}
	catchCom()

    // Create an enumerator with CComEnumOnSTL<> and our copy policy class.
	CComObject<CEnumIDListImpl>* pEnum;
    hr = CComObject<CEnumIDListImpl>::CreateInstance( &pEnum );
	ATLENSURE_RETURN_HR(SUCCEEDED(hr), hr);
    pEnum->AddRef();

    // Init the enumerator.  Init() will AddRef() our IUnknown (obtained with
    // GetUnknown()) so this object will stay alive as long as the enumerator 
    // needs access to the vector m_vecConnData.
    hr = pEnum->Init(GetUnknown(), m_vecConnData);

    // Return an IEnumIDList interface to the caller.
    if (SUCCEEDED(hr))
        hr = pEnum->QueryInterface(__uuidof(IEnumIDList), (void**)ppEnumIDList);

    pEnum->Release();
    return hr;
}

/**
 * Convert path string relative to this folder into a PIDL to the item.
 *
 * @implementing IShellFolder
 *
 * @todo  Handle the attributes parameter.  Should just return
 * GetAttributesOf() the PIDL we create but it is a bit hazy where the
 * host PIDL's responsibilities end and the remote PIDL's start because
 * of the path embedded in the host PIDL.
 */
STDMETHODIMP CHostFolder::ParseDisplayName(
	HWND hwnd, IBindCtx *pbc, PWSTR pwszDisplayName, ULONG *pchEaten,
	PIDLIST_RELATIVE *ppidl, __inout_opt ULONG *pdwAttributes)
{
	ATLTRACE(__FUNCTION__" called (pwszDisplayName=%ws)\n", pwszDisplayName);
	ATLENSURE_RETURN_HR(pwszDisplayName, E_POINTER);
	ATLENSURE_RETURN_HR(*pwszDisplayName != L'\0', E_INVALIDARG);
	ATLENSURE_RETURN_HR(ppidl, E_POINTER);

	// The string we are trying to parse should be of the form:
	//    sftp://username@hostname:port/path

	wstring strDisplayName(pwszDisplayName);
	if (strDisplayName.empty())
	{
		root_pidl().copy_to(*ppidl);
		return S_OK;
	}

	// Must start with sftp://
	ATLENSURE_RETURN(strDisplayName.substr(0, 7) == L"sftp://");

	// Must have @ to separate username from hostname
	wstring::size_type nAt = strDisplayName.find_first_of(L'@', 7);
	ATLENSURE_RETURN(nAt != wstring::npos);

	// Must have : to separate hostname from port number
	wstring::size_type nColon = strDisplayName.find_first_of(L':', 7);
	ATLENSURE_RETURN(nColon != wstring::npos);
	ATLENSURE_RETURN(nColon > nAt);

	// Must have / to separate port number from path
	wstring::size_type nSlash = strDisplayName.find_first_of(L'/', 7);
	ATLENSURE_RETURN(nSlash != wstring::npos);
	ATLENSURE_RETURN(nSlash > nColon);

	wstring strUser = strDisplayName.substr(7, nAt - 7);
	wstring strHost = strDisplayName.substr(nAt+1, nColon - (nAt+1));
	wstring strPort = strDisplayName.substr(nColon+1, nAt - (nSlash+1));
	wstring strPath = strDisplayName.substr(nSlash+1);
	ATLENSURE_RETURN(!strUser.empty());
	ATLENSURE_RETURN(!strHost.empty());
	ATLENSURE_RETURN(!strPort.empty());
	ATLENSURE_RETURN(!strPath.empty());

	int nPort = _wtoi(strPort.c_str());
	ATLENSURE_RETURN(nPort >= MIN_PORT && nPort <= MAX_PORT);

	// Create child PIDL for this path segment
	HRESULT hr = S_OK;
	try
	{
		CHostItem pidl(
			strUser.c_str(), strHost.c_str(), strPath.c_str(),
			static_cast<USHORT>(nPort));

		CComPtr<IShellFolder> spSubfolder;
		hr = BindToObject(pidl, pbc, IID_PPV_ARGS(&spSubfolder));
		ATLENSURE_SUCCEEDED(hr);

		wchar_t wszPath[MAX_PATH];
		::StringCchCopyW(wszPath, ARRAYSIZE(wszPath), strPath.c_str());

		pidl_t pidl_path;
		hr = spSubfolder->ParseDisplayName(
			hwnd, pbc, wszPath, pchEaten, pidl_path.out(), pdwAttributes);
		ATLENSURE_SUCCEEDED(hr);

		pidl_t pidl_out = root_pidl() + pidl_path;
		pidl_out.copy_to(*ppidl);
	}
	catchCom()

	return hr;
}

/**
 * Retrieve the display name for the specified file object or subfolder.
 *
 * @implementing IShellFolder
 */
STDMETHODIMP CHostFolder::GetDisplayNameOf(
	PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *pName)
{
	METHOD_TRACE;
	ATLENSURE_RETURN_HR(!::ILIsEmpty(pidl), E_INVALIDARG);
	ATLENSURE_RETURN_HR(pName, E_POINTER);

	::ZeroMemory(pName, sizeof STRRET);

	try
	{
		wstring name;
		CHostItem hpidl(pidl);

		if (uFlags & SHGDN_FORPARSING)
		{
			if (!(uFlags & SHGDN_INFOLDER))
			{
				// Bind to parent
				CComPtr<IShellFolder> spParent;
				PCUITEMID_CHILD pidlThisFolder;
				HRESULT hr = swish::windows_api::SHBindToParent(
					root_pidl().get(), IID_PPV_ARGS(&spParent),
					&pidlThisFolder);
				ATLASSERT(SUCCEEDED(hr));

				STRRET strret;
				::ZeroMemory(&strret, sizeof strret);
				hr = spParent->GetDisplayNameOf(
					pidlThisFolder, uFlags, &strret);
				if (FAILED(hr))
					BOOST_THROW_EXCEPTION(com_exception(hr));

				name = strret_to_string<wchar_t>(
					strret, pidlThisFolder) + L'\\';
			}

			name += hpidl.GetLongName(true);
		}
		else if (uFlags == SHGDN_NORMAL || uFlags & SHGDN_FORADDRESSBAR)
		{
			name = hpidl.GetLongName(false);
		}
		else if (uFlags == SHGDN_INFOLDER || uFlags & SHGDN_FOREDITING)
		{
			name = hpidl.GetLabel();
		}
		else
		{
			UNREACHABLE;
			return E_INVALIDARG;
		}

		// Store in a STRRET and return
		pName->uType = STRRET_WSTR;

		return SHStrDupW( name.c_str(), &pName->pOleStr );
	}
	catchCom()
}

/**
 * Returns the attributes for the items whose PIDLs are passed in.
 *
 * @implementing IShellFolder
 */
STDMETHODIMP CHostFolder::GetAttributesOf(
	UINT cIdl,
	__in_ecount_opt( cIdl ) PCUITEMID_CHILD_ARRAY aPidl,
	__inout SFGAOF *pdwAttribs )
{
	ATLTRACE("CHostFolder::GetAttributesOf called\n");
	(void)aPidl; // All items are folders. No need to check PIDL.
	(void)cIdl;

	DWORD dwAttribs = 0;
    dwAttribs |= SFGAO_FOLDER;
    dwAttribs |= SFGAO_HASSUBFOLDER;
    *pdwAttribs &= dwAttribs;

    return S_OK;
}

/**
 * Convert column to appropriate property set ID (FMTID) and property ID (PID).
 *
 * @implementing IShellFolder2
 *
 * @note   The first column for which we return an error, marks the end of the
 *         columns in this folder.
 */
STDMETHODIMP CHostFolder::MapColumnToSCID(UINT iColumn, PROPERTYKEY* pscid)
{
	if (!pscid) return E_POINTER;
	std::memset(pscid, 0, sizeof(PROPERTYKEY));

	try
	{
		*pscid = property_key_from_column_index(iColumn).get();
	}
	catchCom()

	return S_OK;
}

/**
 * Extract an icon bitmap given the information passed.
 *
 * @implementing IExtractIconW
 *
 * We return S_FALSE to tell the shell to extract the icons itself.
 */
STDMETHODIMP CHostFolder::Extract( LPCTSTR, UINT, HICON *, HICON *, UINT )
{
	ATLTRACE("CHostFolder::Extract called\n");
	return S_FALSE;
}

/**
 * Retrieve the location of the appropriate icon.
 *
 * @implementing IExtractIconW
 *
 * We set all SFTP hosts to have the icon from shell32.dll.
 */
STDMETHODIMP CHostFolder::GetIconLocation(
	__in UINT uFlags, __out_ecount(cchMax) LPTSTR szIconFile, 
	__in UINT cchMax, __out int *piIndex, __out UINT *pwFlags )
{
	ATLTRACE("CHostFolder::GetIconLocation called\n");
	(void)uFlags; // type of use is ignored for host folder

	// Set host to have the ICS host icon
	StringCchCopy(szIconFile, cchMax, L"shell32.dll");
	*piIndex = 17;
	*pwFlags = GIL_DONTCACHE;

	return S_OK;
}

/*--------------------------------------------------------------------------*/
/*                     CFolder NVI internal interface.                      */
/* These method implement the internal interface of the CFolder abstract    */
/* class                                                                    */
/*--------------------------------------------------------------------------*/

/**
 * Return the folder's registered CLSID
 *
 * @implementing CFolder
 */
CLSID CHostFolder::clsid() const
{
	return __uuidof(this);
}

/**
 * Sniff PIDLs to determine if they are of our type.  Throw if not.
 *
 * @implementing CFolder
 */
void CHostFolder::validate_pidl(PCUIDLIST_RELATIVE pidl) const
{
	if (pidl == NULL)
		throw com_exception(E_POINTER);

	if (!CHostItemList::IsValid(pidl))
		throw com_exception(E_INVALIDARG);
}

/**
 * Create and initialise new folder object for subfolder.
 *
 * @implementing CFolder
 *
 * Create CRemoteFolder initialised with its root PIDL.  CHostFolders
 * don't have any other types of subfolder.
 */
CComPtr<IShellFolder> CHostFolder::subfolder(const apidl_t& pidl) const
{
	CComPtr<IShellFolder> folder = CRemoteFolder::Create(pidl.get());
	ATLENSURE_THROW(folder, E_NOINTERFACE);

	return folder;
}

/**
 * Return a property, specified by PROERTYKEY, of an item in this folder.
 */
variant_t CHostFolder::property(const property_key& key, const cpidl_t& pidl)
{
	return property_from_pidl(pidl, key);
}

/*--------------------------------------------------------------------------*/
/*                    CSwishFolder internal interface.                      */
/* These method override the (usually no-op) implementations of some        */
/* in the CSwishFolder base class                                           */
/*--------------------------------------------------------------------------*/


/**
 * Create a toolbar command provider for the folder.
 */
CComPtr<IExplorerCommandProvider> CHostFolder::command_provider(
	HWND hwnd)
{
	TRACE("Request: IExplorerCommandProvider");
	return host_folder_command_provider(hwnd, root_pidl()).get();
}

/**
 * Create an icon extraction helper object for the selected item.
 *
 * @implementing CSwishFolder
 *
 * For host folders, the extraction object happens to be the folder
 * itself. We don't need to look at the PIDLs as all host items are the same.
 */
CComPtr<IExtractIconW> CHostFolder::extract_icon_w(
	HWND /*hwnd*/, PCUITEMID_CHILD /*pidl*/)
{
	TRACE("Request: IExtractIconW");

	return this;
}

/**
 * Create a file association handler for host items.
 *
 * @implementing CSwishFolder
 *
 * We don't need to look at the PIDLs as all host items are the same.
 */
CComPtr<IQueryAssociations> CHostFolder::query_associations(
	HWND /*hwnd*/, UINT /*cpidl*/, PCUITEMID_CHILD_ARRAY /*apidl*/)
{
	TRACE("Request: IQueryAssociations");

	CComPtr<IQueryAssociations> spAssoc;
	HRESULT hr = ::AssocCreate(
		CLSID_QueryAssociations, IID_PPV_ARGS(&spAssoc));
	ATLENSURE_SUCCEEDED(hr);

	// Get CLSID in {DWORD-WORD-WORD-WORD-WORD.DWORD} form
	LPOLESTR posz;
	::StringFromCLSID(__uuidof(CHostFolder), &posz);
	shared_ptr<OLECHAR> clsid(posz, ::CoTaskMemFree);
	posz = NULL;

	// Initialise default assoc provider to use Swish CLSID key for data.
	// This is necessary to pick up properties and TileInfo etc.
	hr = spAssoc->Init(0, clsid.get(), NULL, NULL);
	if (FAILED(hr))
		throw com_exception(hr);

	return spAssoc;
}

/**
 * Create a context menu for the selected items.
 *
 * @implementing CSwishFolder
 */
CComPtr<IContextMenu> CHostFolder::context_menu(
	HWND hwnd, UINT cpidl, PCUITEMID_CHILD_ARRAY apidl)
{
	TRACE("Request: IContextMenu");
	assert(cpidl > 0);

	// Get keys associated with filetype from registry.
	// We only take into account the item that was right-clicked on 
	// (the first array element) even if this was a multi-selection.
	//
	// This article says that we don't need to specify the keys:
	// http://groups.google.com/group/microsoft.public.platformsdk.shell/
	// browse_thread/thread/6f07525eaddea29d/
	// but we do for the context menu to appear in versions of Windows 
	// earlier than Vista.
	HKEY *akeys; UINT ckeys;
	ATLENSURE_THROW(SUCCEEDED(
		CRegistry::GetHostFolderAssocKeys(&ckeys, &akeys)),
		E_UNEXPECTED  // Might fail if registry is corrupted
	);

	CComPtr<IShellFolder> spThisFolder = this;
	ATLENSURE_THROW(spThisFolder, E_OUTOFMEMORY);

	// Create default context menu from list of PIDLs
	CComPtr<IContextMenu> spMenu;
	HRESULT hr = ::CDefFolderMenu_Create2(
		root_pidl().get(), hwnd, cpidl, apidl, spThisFolder, 
		MenuCallback, ckeys, akeys, &spMenu);
	if (FAILED(hr))
		throw com_exception(hr);

	return spMenu;
}

/**
 * Create a data object for the selected items.
 *
 * @implementing CSwishFolder
 */
CComPtr<IDataObject> CHostFolder::data_object(
	HWND /*hwnd*/, UINT cpidl, PCUITEMID_CHILD_ARRAY apidl)
{
	TRACE("Request: IDataObject");
	assert(cpidl > 0);

	// A DataObject is required in order for the call to 
	// CDefFolderMenu_Create2 (above) to succeed on versions of Windows
	// earlier than Vista

	CComPtr<IDataObject> spdo;
	HRESULT hr = ::CIDLData_CreateFromIDArray(
		root_pidl().get(), cpidl, 
		reinterpret_cast<PCUIDLIST_RELATIVE_ARRAY>(apidl), &spdo);
	if (FAILED(hr))
		throw com_exception(hr);

	return spdo;
}

/**
 * Create an instance of our Shell Folder View callback handler.
 *
 * @implementing CSwishFolder
 */
CComPtr<IShellFolderViewCB> CHostFolder::folder_view_callback(HWND /*hwnd*/)
{
	return CExplorerCallback::Create(root_pidl().get());
}


/*--------------------------------------------------------------------------*/
/*                         Context menu handlers.                           */
/*--------------------------------------------------------------------------*/

/**
 * Cracks open the @c DFM_* callback messages and dispatched them to handlers.
 */
HRESULT CHostFolder::OnMenuCallback(
	HWND hwnd, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	METHOD_TRACE("(uMsg=%d)\n", uMsg);
	UNREFERENCED_PARAMETER(hwnd);

	switch (uMsg)
	{
	case DFM_MERGECONTEXTMENU:
		return this->OnMergeContextMenu(
			hwnd,
			pdtobj,
			static_cast<UINT>(wParam),
			*reinterpret_cast<QCMINFO *>(lParam)
		);
	default:
		return S_FALSE;
	}
}

/**
 * Handle @c DFM_MERGECONTEXTMENU callback.
 */
HRESULT CHostFolder::OnMergeContextMenu(
	HWND hwnd, IDataObject *pDataObj, UINT uFlags, QCMINFO& info )
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(pDataObj);
	UNREFERENCED_PARAMETER(uFlags);
	UNREFERENCED_PARAMETER(info);

	// It seems we have to return S_OK even if we do nothing else or Explorer
	// won't put Open as the default item and in the right order
	return S_OK;
}
