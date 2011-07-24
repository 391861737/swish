/**
    @file

    Explorer folder that handles remote files and folders.

    @if license

    Copyright (C) 2007, 2008, 2009, 2010, 2011
    Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "RemoteFolder.h"

#include "SftpDirectory.h"
#include "SftpDataObject.h"
#include "IconExtractor.h"
#include "data_object/ShellDataObject.hpp"  // PidlFormat
#include "Registry.h"
#include "swish/debug.hpp"
#include "swish/drop_target/SnitchingDropTarget.hpp" // CSnitchingDropTarget
#include "swish/drop_target/DropUI.hpp" // DropUI
#include "swish/frontend/announce_error.hpp" // rethrow_and_announce
#include "swish/remote_folder/columns.hpp" // property_key_from_column_index
#include "swish/remote_folder/commands.hpp" // remote_folder_command_provider
#include "swish/remote_folder/connection.hpp" // connection_from_pidl
#include "swish/remote_folder/properties.hpp" // property_from_pidl
#include "swish/remote_folder/remote_pidl.hpp" // remote_itemid_view
                                               // create_remote_itemdid
#include "swish/remote_folder/symlink.hpp" // pidl_to_shell_link
#include "swish/remote_folder/ViewCallback.hpp" // CViewCallback
#include "swish/remote_folder/swish_pidl.hpp" // absolute_path_from_swish_pidl
#include "swish/trace.hpp" // trace
#include "swish/windows_api.hpp" // SHBindToParent

#include <winapi/gui/message_box.hpp> // message_box
#include <winapi/shell/shell.hpp> // string_to_strret

#include <comet/bstr.h> // bstr_t
#include <comet/datetime.h> // datetime_t

#include <boost/bind.hpp> // bind
#include <boost/exception/diagnostic_information.hpp> // diagnostic_information
#include <boost/filesystem/path.hpp> // wpath
#include <boost/locale.hpp> // translate
#include <boost/make_shared.hpp> // make_shared
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <cassert> // assert
#include <string>

using swish::drop_target::CSnitchingDropTarget;
using swish::drop_target::DropUI;
using swish::frontend::rethrow_and_announce;
using swish::remote_folder::absolute_path_from_swish_pidl;
using swish::remote_folder::commands::remote_folder_command_provider;
using swish::remote_folder::connection_from_pidl;
using swish::remote_folder::create_remote_itemid;
using swish::remote_folder::CViewCallback;
using swish::remote_folder::pidl_to_shell_link;
using swish::remote_folder::property_from_pidl;
using swish::remote_folder::property_key_from_column_index;
using swish::remote_folder::remote_itemid_view;
using swish::shell_folder::data_object::PidlFormat;
using swish::tracing::trace;

using namespace winapi::gui::message_box;
using winapi::shell::pidl::apidl_t;
using winapi::shell::pidl::cpidl_t;
using winapi::shell::pidl::pidl_t;
using winapi::shell::property_key;
using winapi::shell::string_to_strret;

using comet::bstr_t;
using comet::com_ptr;
using comet::com_error;
using comet::datetime_t;
using comet::throw_com_error;
using comet::variant_t;

using boost::bind;
using boost::filesystem::wpath;
using boost::locale::translate;
using boost::make_shared;

using ATL::CComPtr;
using ATL::CString;

using std::vector;
using std::wstring;

namespace {

	cpidl_t create_filename_only_pidl(const wstring& filename)
	{
		return create_remote_itemid(
			filename, false, false, L"", L"", 0, 0, 0, 0, datetime_t(),
			datetime_t());
	}

	/**
	 * Remove the extension from the remote item's filename *if appropriate*.
	 */
	wstring filename_without_extension(const cpidl_t remote_item)
	{
		remote_itemid_view itemid(remote_item);
		wstring full_name = itemid.filename();

		if (full_name.empty() || itemid.is_folder())
		{
			return full_name;
		}
		else
		{
			if (full_name[0] != L'.')
			{
				return wpath(full_name).stem();
			}
			else
			{
				// File might look something like '.hidden.txt' or it might
				// just be '.hidden'.  In the first case we only want to remove
				// the '.txt' extension.  In the second case we don't want
				// to remove anything.
				wstring bit_after_initial_dot = full_name.substr(1);
				return L'.' + wpath(bit_after_initial_dot).stem();
			}
		}
	}
}

/*--------------------------------------------------------------------------*/
/*      Functions implementing IShellFolder via folder_error_adapter.       */
/*--------------------------------------------------------------------------*/

/**
 * Create an IEnumIDList which enumerates the items in this folder.
 *
 * @implementing folder_error_adapter
 *
 * @param hwnd   Optional window handle used if enumeration requires user
 *               input.
 * @param flags  Flags specifying which types of items to include in the
 *               enumeration. Possible flags are from the @c SHCONT enum.
 */
IEnumIDList* CRemoteFolder::enum_objects(HWND hwnd, SHCONTF flags)
{
	try
	{
		// Create SFTP connection for this folder using hwndOwner for UI
		com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);

		// Create directory handler and get listing as PIDL enumeration
		CSftpDirectory directory(
			root_pidl().get(), provider.get(), m_consumer.get());
		return directory.GetEnum(flags).detach();
	}
	catch (...)
	{
		rethrow_and_announce(
			hwnd, translate("Unable to access the directory"),
			translate("You might not have permission."));
	}
}

/**
 * Convert path string relative to this folder into a PIDL to the item.
 *
 * @implementing folder_error_adapter
 *
 * @todo  Handle the attributes parameter.  Will need to contact server
 * as the PIDL we create is fake and will not have correct folderness, etc.
 */
PIDLIST_RELATIVE CRemoteFolder::parse_display_name(
	HWND hwnd, IBindCtx* bind_ctx, const wchar_t* display_name,
	ULONG* attributes_inout)
{
	try
	{
		trace(__FUNCTION__" called (display_name=%s)") % display_name;
		if (*display_name == L'\0')
			BOOST_THROW_EXCEPTION(com_error(E_INVALIDARG));

		// The string we are trying to parse should be of the form:
		//    directory/directory/filename
		// or 
		//    filename
		wstring strDisplayName(display_name);

		// May have / to separate path segments
		wstring::size_type nSlash = strDisplayName.find_first_of(L'/');
		wstring strSegment;
		if (nSlash == 0) // Unix machine - starts with folder called /
		{
			strSegment = strDisplayName.substr(0, 1);
		}
		else
		{
			strSegment = strDisplayName.substr(0, nSlash);
		}

		// Create child PIDL for this path segment
		cpidl_t pidl = create_filename_only_pidl(strSegment);

		// Bind to subfolder and recurse if there were other path segments
		if (nSlash != wstring::npos)
		{
			wstring strRest = strDisplayName.substr(nSlash+1);

			com_ptr<IShellFolder> subfolder;
			bind_to_object(
				pidl.get(), bind_ctx, subfolder.iid(),
				reinterpret_cast<void**>(subfolder.out()));

			wchar_t wszRest[MAX_PATH];
			::wcscpy_s(wszRest, ARRAYSIZE(wszRest), strRest.c_str());

			pidl_t rest;
			HRESULT hr = subfolder->ParseDisplayName(
				hwnd, bind_ctx, wszRest, NULL, rest.out(), attributes_inout);
			if (FAILED(hr))
				throw_com_error(subfolder.get(), hr);

			return (pidl + rest).detach();
		}
		else
		{
			return pidl.detach();
		}
	}
	catch (...)
	{
		rethrow_and_announce(
			hwnd, translate("Path not recognised"),
			translate("Check that the path was entered correctly."));
	}
}

/**
 * Retrieve the display name for the specified file object or subfolder.
 *
 * @implementing folder_error_adapter
 */
STRRET CRemoteFolder::get_display_name_of(PCUITEMID_CHILD pidl, SHGDNF flags)
{
	if (::ILIsEmpty(pidl))
		BOOST_THROW_EXCEPTION(com_error(E_INVALIDARG));

	wstring name;

	bool fForParsing = (flags & SHGDN_FORPARSING) != 0;

	if (fForParsing || (flags & SHGDN_FORADDRESSBAR))
	{
		if (!(flags & SHGDN_INFOLDER))
		{
			// Bind to parent
			com_ptr<IShellFolder> parent;
			PCUITEMID_CHILD pidlThisFolder = NULL;
			HRESULT hr = swish::windows_api::SHBindToParent(
				root_pidl().get(), parent.iid(),
				reinterpret_cast<void**>(parent.out()), &pidlThisFolder);
			
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_error(hr));

			STRRET strret;
			std::memset(&strret, 0, sizeof(strret));
			hr = parent->GetDisplayNameOf(pidlThisFolder, flags, &strret);
			if (FAILED(hr))
				throw_com_error(parent.get(), hr);

			ATLASSERT(strret.uType == STRRET_WSTR);

			name += strret.pOleStr;
			name += L'/';
		}

		// Add child path - include extension if FORPARSING
		if (fForParsing)
			name += remote_itemid_view(pidl).filename();
		else
			name += filename_without_extension(pidl);

	}
	else if (flags & SHGDN_FOREDITING)
	{
		name = remote_itemid_view(pidl).filename();
	}
	else
	{
		ATLASSERT(flags == SHGDN_NORMAL || flags == SHGDN_INFOLDER);

		name = filename_without_extension(pidl);
	}

	return string_to_strret(name);
}

/**
 * Rename item.
 *
 * @implementing folder_error_adapter
 */
PITEMID_CHILD CRemoteFolder::set_name_of(
	HWND hwnd, PCUITEMID_CHILD pidl, const wchar_t* name, SHGDNF /*flags*/)
{
	try
	{
		// Create SFTP connection object for this folder using hwnd for UI
		com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);

		// Rename file
		CSftpDirectory directory(
			root_pidl().get(), provider.get(), m_consumer.get());
		bool fOverwritten = directory.Rename(pidl, name);

		// Create new PIDL from old one with new filename
		remote_itemid_view itemid(pidl);
		cpidl_t new_file = create_remote_itemid(
			name, itemid.is_folder(), itemid.is_link(),
			itemid.owner(), itemid.group(), itemid.owner_id(),
			itemid.group_id(), itemid.permissions(), itemid.size(),
			itemid.date_modified(), itemid.date_accessed());

		// A failure to notify the shell shouldn't prevent us returning the PIDL
		try
		{
			// Make PIDLs absolute
			apidl_t old_pidl = root_pidl() + pidl;
			apidl_t new_pidl = root_pidl() + new_file;

			// Update the shell by passing both PIDLs
			if (fOverwritten)
			{
				::SHChangeNotify(
					SHCNE_DELETE, SHCNF_IDLIST | SHCNF_FLUSH, new_pidl.get(),
					NULL);
			}
			::SHChangeNotify(
				(remote_itemid_view(pidl).is_folder()) ?
					SHCNE_RENAMEFOLDER : SHCNE_RENAMEITEM,
				SHCNF_IDLIST | SHCNF_FLUSH, old_pidl.get(), new_pidl.get());
		}
		catch (const std::exception& e)
		{
			trace("Exception thrown while notifying shell of rename:");
			trace("%s") % boost::diagnostic_information(e);
		}

		return new_file.detach();
	}
	catch (...)
	{
		rethrow_and_announce(
			hwnd, translate("Unable to rename the item"),
			translate("You might not have permission."));
	}
}

/**
 * Returns the attributes for the items whose PIDLs are passed in.
 *
 * @implementing folder_error_adapter
 */
void CRemoteFolder::get_attributes_of(
	UINT pidl_count, PCUITEMID_CHILD_ARRAY pidl_array,
	SFGAOF* attributes_inout)
{
	// Search through all PIDLs and check if they are all folders
	bool fAllAreFolders = true;
	for (UINT i = 0; i < pidl_count; i++)
	{
		if (!remote_itemid_view(pidl_array[i]).is_folder())
		{
			fAllAreFolders = false;
			break;
		}
	}

	// Search through all PIDLs and check if they are all links
	bool fAreAllLinks = true;
	for (UINT i = 0; i < pidl_count; i++)
	{
		if (!remote_itemid_view(pidl_array[i]).is_link())
		{
			fAreAllLinks = false;
			break;
		}
	}

	// Search through all PIDLs and check if they are all 'dot' files
	bool fAllAreDotFiles = true;
	for (UINT i = 0; i < pidl_count; i++)
	{
		wstring filename = remote_itemid_view(pidl_array[i]).filename();
		if (filename[0] != L'.')
		{
			fAllAreDotFiles = false;
			break;
		}
	}

	DWORD dwAttribs = 0;
	if (fAllAreFolders)
	{
		dwAttribs |= SFGAO_FOLDER;
		dwAttribs |= SFGAO_HASSUBFOLDER;
		dwAttribs |= SFGAO_DROPTARGET;
	}
	if (fAllAreDotFiles)
	{
		dwAttribs |= SFGAO_GHOSTED;
		dwAttribs |= SFGAO_HIDDEN;
	}
	if (fAreAllLinks)
	{
		dwAttribs |= SFGAO_LINK;
	}
	dwAttribs |= SFGAO_CANRENAME;
	dwAttribs |= SFGAO_CANDELETE;
	dwAttribs |= SFGAO_CANCOPY;

    *attributes_inout &= dwAttribs;
}

/*--------------------------------------------------------------------------*/
/*     Functions implementing IShellFolder2 via folder2_error_adapter.      */
/*--------------------------------------------------------------------------*/

/**
 * Convert column index to matching PROPERTYKEY, if any.
 *
 * @implementing folder2_error_adapter
 */
SHCOLUMNID CRemoteFolder::map_column_to_scid(UINT column_index)
{
	return property_key_from_column_index(column_index).get();
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
CLSID CRemoteFolder::clsid() const
{
	return __uuidof(this);
}

/**
 * Sniff PIDLs to determine if they are of our type.  Throw if not.
 *
 * @implementing CFolder
 */
void CRemoteFolder::validate_pidl(PCUIDLIST_RELATIVE pidl) const
{
	if (pidl == NULL)
		BOOST_THROW_EXCEPTION(com_error(E_POINTER));

	if (!remote_itemid_view(pidl).valid())
		BOOST_THROW_EXCEPTION(com_error(E_INVALIDARG));
}

/**
 * Create and initialise new folder object for subfolder.
 *
 * @implementing CFolder
 *
 * Create new CRemoteFolder initialised with its root PIDL.  CRemoteFolder
 * only have instances of themselves as subfolders.
 */
CComPtr<IShellFolder> CRemoteFolder::subfolder(const apidl_t& pidl) const
{
	// Create CRemoteFolder initialised with its root PIDL
	CComPtr<IShellFolder> folder = CRemoteFolder::Create(
		pidl.get(), m_consumer_factory);
	ATLENSURE_THROW(folder, E_NOINTERFACE);

	return folder;
}

/**
 * Return a property, specified by PROERTYKEY, of an item in this folder.
 */
variant_t CRemoteFolder::property(const property_key& key, const cpidl_t& pidl)
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
CComPtr<IExplorerCommandProvider> CRemoteFolder::command_provider(HWND hwnd)
{
	TRACE("Request: IExplorerCommandProvider");
	com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);
	return remote_folder_command_provider(
		hwnd, root_pidl(),
		bind(&connection_from_pidl, root_pidl(), hwnd),
		bind(m_consumer_factory, hwnd)).get();
}

/**
 * Create an icon extraction helper object for the selected item.
 *
 * @implementing CSwishFolder
 */
CComPtr<IExtractIconW> CRemoteFolder::extract_icon_w(
	HWND /*hwnd*/, PCUITEMID_CHILD pidl)
{
	TRACE("Request: IExtractIconW");

	remote_itemid_view itemid(pidl);
	return CIconExtractor::Create(
		itemid.filename().c_str(), itemid.is_folder());
}

/**
 * @implementing CSwishFolder
 */
CComPtr<IShellLinkW> CRemoteFolder::shell_link_w(
	HWND hwnd, PCUITEMID_CHILD pidl)
{
	assert(remote_itemid_view(pidl).is_link());

	// Create connection for this folder with hwnd for UI
	com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);

	return pidl_to_shell_link(root_pidl(), pidl, provider, m_consumer).get();
}

/**
 * Create a file association handler for the selected items.
 *
 * @implementing CSwishFolder
 */
CComPtr<IQueryAssociations> CRemoteFolder::query_associations(
	HWND hwnd, UINT cpidl, PCUITEMID_CHILD_ARRAY apidl)
{
	TRACE("Request: IQueryAssociations");
	ATLENSURE(cpidl > 0);

	CComPtr<IQueryAssociations> spAssoc;
	HRESULT hr = ::AssocCreate(
		CLSID_QueryAssociations, IID_PPV_ARGS(&spAssoc));
	ATLENSURE_SUCCEEDED(hr);

	remote_itemid_view itemid(apidl[0]);
	
	if (itemid.is_folder())
	{
		// Initialise default assoc provider for Folders
		hr = spAssoc->Init(
			ASSOCF_INIT_DEFAULTTOFOLDER, L"Folder", NULL, hwnd);
		ATLENSURE_SUCCEEDED(hr);
	}
	else
	{
		// Initialise default assoc provider for given file extension
		wstring extension = wpath(itemid.filename()).extension();
		if (extension.empty())
			extension = L".";
		hr = spAssoc->Init(
			ASSOCF_INIT_DEFAULTTOSTAR, extension.c_str(), NULL, hwnd);
		ATLENSURE_SUCCEEDED(hr);
	}

	return spAssoc;
}

/**
 * Create a context menu for the selected items.
 *
 * @implementing CSwishFolder
 */
CComPtr<IContextMenu> CRemoteFolder::context_menu(
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
	HKEY *akeys = NULL;
	UINT ckeys = 0;
	if (cpidl > 0)
	{
		ATLENSURE_THROW(SUCCEEDED(
			CRegistry::GetRemoteFolderAssocKeys(
				remote_itemid_view(apidl[0]), &ckeys, &akeys)),
			E_UNEXPECTED  // Might fail if registry is corrupted
		);
	}

	CComPtr<IShellFolder> spThisFolder = this;
	ATLENSURE_THROW(spThisFolder, E_OUTOFMEMORY);

	// Create default context menu from list of PIDLs
	CComPtr<IContextMenu> spMenu;
	HRESULT hr = ::CDefFolderMenu_Create2(
		root_pidl().get(), hwnd, cpidl, apidl, spThisFolder, 
		MenuCallback, ckeys, akeys, &spMenu);
	if (FAILED(hr))
		BOOST_THROW_EXCEPTION(com_error(hr));

	return spMenu;
}

CComPtr<IContextMenu> CRemoteFolder::background_context_menu(HWND hwnd)
{
	TRACE("Request: IContextMenu");

	// Get keys associated with directory background menus from registry.
	//
	// This article says that we don't need to specify the keys:
	// http://groups.google.com/group/microsoft.public.platformsdk.shell/
	// browse_thread/thread/6f07525eaddea29d/
	// but we do for the context menu to appear in versions of Windows 
	// earlier than Vista.
	HKEY *akeys; UINT ckeys;
	ATLENSURE_THROW(SUCCEEDED(
		CRegistry::GetRemoteFolderBackgroundAssocKeys(&ckeys, &akeys)),
		E_UNEXPECTED  // Might fail if registry is corrupted
		);

	CComPtr<IShellFolder> spThisFolder = this;
	ATLENSURE_THROW(spThisFolder, E_OUTOFMEMORY);

	// Create default context menu from list of PIDLs
	CComPtr<IContextMenu> spMenu;
	HRESULT hr = ::CDefFolderMenu_Create2(
		root_pidl().get(), hwnd, 0, NULL, spThisFolder, 
		MenuCallback, ckeys, akeys, &spMenu);
	if (FAILED(hr))
		BOOST_THROW_EXCEPTION(com_error(hr));

	return spMenu;
}

/**
 * Create a data object for the selected items.
 *
 * @implementing CSwishFolder
 */
CComPtr<IDataObject> CRemoteFolder::data_object(
	HWND hwnd, UINT cpidl, PCUITEMID_CHILD_ARRAY apidl)
{
	TRACE("Request: IDataObject");
	assert(cpidl > 0);

	try
	{
		// Create connection for this folder with hwnd for UI
		CComPtr<ISftpProvider> spProvider =
			_CreateConnectionForFolder(hwnd).get();

		return CSftpDataObject::Create(
			cpidl, apidl, root_pidl().get(), spProvider, m_consumer.get());
	}
	catch (...)
	{
		rethrow_and_announce(
			hwnd,
			(cpidl > 1) ? translate("Unable to access the item") :
			              translate("Unable to access the items"),
			translate("You might not have permission."));
	}
}

/**
 * Create a drop target handler for the folder.
 *
 * @implementing CSwishFolder
 */
CComPtr<IDropTarget> CRemoteFolder::drop_target(HWND hwnd)
{
	TRACE("Request: IDropTarget");

	try
	{
		// Create connection for this folder with hwnd for UI
		com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);
		return new CSnitchingDropTarget(
			hwnd, provider, m_consumer,
			absolute_path_from_swish_pidl(root_pidl()),
			make_shared<DropUI>(hwnd));
	}
	catch (...)
	{
		rethrow_and_announce(
			hwnd, translate("Unable to access the folder"),
			translate("You might not have permission."));
	}
}

/**
 * Create an instance of our Shell Folder View callback handler.
 */
CComPtr<IShellFolderViewCB> CRemoteFolder::folder_view_callback(HWND /*hwnd*/)
{
	return new CViewCallback(root_pidl());
}


/*--------------------------------------------------------------------------*/
/*                         Context menu handlers.                           */
/*--------------------------------------------------------------------------*/

/**
 * Cracks open the @c DFM_* callback messages and dispatched them to handlers.
 */
HRESULT CRemoteFolder::OnMenuCallback(
	HWND hwnd, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	ATLTRACE(__FUNCTION__" called (uMsg=%d)\n", uMsg);
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
	case DFM_INVOKECOMMAND:
		return this->OnInvokeCommand(
			hwnd,
			pdtobj,
			static_cast<int>(wParam),
			reinterpret_cast<PCWSTR>(lParam)
		);
	case DFM_INVOKECOMMANDEX:
		return this->OnInvokeCommandEx(
			hwnd,
			pdtobj,
			static_cast<int>(wParam),
			reinterpret_cast<PDFMICS>(lParam)
		);
	case DFM_GETDEFSTATICID:
		return S_FALSE;
	default:
		return E_NOTIMPL;
	}
}

/**
 * Handle @c DFM_MERGECONTEXTMENU callback.
 */
HRESULT CRemoteFolder::OnMergeContextMenu(
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

/**
 * Handle @c DFM_INVOKECOMMAND callback.
 */
HRESULT CRemoteFolder::OnInvokeCommand(
	HWND hwnd, IDataObject *pDataObj, int idCmd, PCWSTR pwszArgs )
{
	ATLTRACE(__FUNCTION__" called (hwnd=%p, pDataObj=%p, idCmd=%d, "
		"pwszArgs=%ls)\n", hwnd, pDataObj, idCmd, pwszArgs);

	hwnd; pDataObj; idCmd; pwszArgs; // Unused in Release build
	return S_FALSE;
}

/**
 * Handle @c DFM_INVOKECOMMANDEX callback.
 */
HRESULT CRemoteFolder::OnInvokeCommandEx(
	HWND hwnd, IDataObject *pDataObj, int idCmd, PDFMICS pdfmics )
{
	ATLTRACE(__FUNCTION__" called (pDataObj=%p, idCmd=%d, pdfmics=%p)\n",
		pDataObj, idCmd, pdfmics);

	pdfmics; // Unused in Release build

	switch (idCmd)
	{
	case DFM_CMD_DELETE:
		return this->OnCmdDelete(hwnd, pDataObj);
	default:
		return S_FALSE;
	}
}

/**
 * Handle @c DFM_CMD_DELETE verb.
 */
HRESULT CRemoteFolder::OnCmdDelete( HWND hwnd, IDataObject *pDataObj )
{
	ATLTRACE(__FUNCTION__" called (hwnd=%p, pDataObj=%p)\n", hwnd, pDataObj);

	try
	{
		try
		{
			PidlFormat format(pDataObj);
			assert(::ILIsEqual(root_pidl().get(), format.parent_folder().get()));

			// Build up a list of PIDLs for all the items to be deleted
			vector<cpidl_t> death_row;
			for (UINT i = 0; i < format.pidl_count(); i++)
			{
				death_row.push_back(
					winapi::shell::pidl::pidl_cast<cpidl_t>(format.relative_file(i)));
			}

			// Delete
			_Delete(hwnd, death_row);
		}
		catch (...)
		{
			rethrow_and_announce(
				hwnd, translate("Unable to delete the item"),
				translate("You might not have permission."));
		}
	}
	WINAPI_COM_CATCH();

	return S_OK;
}


/*----------------------------------------------------------------------------*/
/*                           Private functions                                */
/*----------------------------------------------------------------------------*/

/**
 * Deletes one or more files or folders after seeking confirmation from user.
 *
 * The list of items to delete is supplied as a list of PIDLs and may contain
 * a mix of files and folders.
 *
 * If just one item is chosen, a specific confirmation message for that item is
 * shown.  If multiple items are to be deleted, a general confirmation message 
 * is displayed asking if the number of items are to be deleted.
 *
 * @param hwnd         Handle to the window used for UI.
 * @param death_row    Collection of items to be deleted as PIDLs.
 *
 * @throws AtlException if a failure occurs.
 */
void CRemoteFolder::_Delete(HWND hwnd, const vector<cpidl_t>& death_row)
{
	size_t cItems = death_row.size();

	BOOL fGoAhead = false;
	if (cItems == 1)
	{
		remote_itemid_view itemid(death_row[0]);
		fGoAhead = _ConfirmDelete(
			hwnd, bstr_t(itemid.filename()).in(), itemid.is_folder());
	}
	else if (cItems > 1)
	{
		fGoAhead = _ConfirmMultiDelete(hwnd, cItems);
	}
	else
	{
		UNREACHABLE;
		AtlThrow(E_UNEXPECTED);
	}

	if (fGoAhead)
		_DoDelete(hwnd, death_row);
}

/**
 * Deletes files or folders.
 *
 * The list of items to delete is supplied as a list of PIDLs and may contain
 * a mix of files and folder.
 *
 * @param hwnd         Handle to the window used for UI.
 * @param death_row    Collection of items to be deleted as PIDLs.
 *
 * @throws AtlException if a failure occurs.
 */
void CRemoteFolder::_DoDelete(HWND hwnd, const vector<cpidl_t>& death_row)
{
	if (hwnd == NULL)
		AtlThrow(E_FAIL);

	// Create SFTP connection object for this folder using hwndOwner for UI
	com_ptr<ISftpProvider> provider = _CreateConnectionForFolder(hwnd);

	// Create instance of our directory handler class
	CSftpDirectory directory(
		root_pidl().get(), provider.get(), m_consumer.get());

	// Delete each item and notify shell
	vector<cpidl_t>::const_iterator it = death_row.begin();
	while (it != death_row.end())
	{
		directory.Delete(*it);

		// Notify the shell
		::SHChangeNotify(
			(remote_itemid_view(*it).is_folder()) ? SHCNE_RMDIR : SHCNE_DELETE,
			SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
			(root_pidl() + *it).get(), NULL
		);

		it++;
	}
}

/**
 * Displays dialog seeking confirmation from user to delete a single item.
 *
 * The dialog differs depending on whether the item is a file or a folder.
 *
 * @param hwnd       Handle to the window used for UI.
 * @param bstrName   Name of the file or folder being deleted.
 * @param fIsFolder  Is the item in question a file or a folder?
 *
 * @returns  Whether confirmation was given or denied.
 */
bool CRemoteFolder::_ConfirmDelete( HWND hwnd, BSTR bstrName, bool fIsFolder )
{
	if (hwnd == NULL)
		return false;

	CString strMessage;
	if (!fIsFolder)
	{
		strMessage = L"Are you sure you want to permanently delete '";
		strMessage += bstrName;
		strMessage += L"'?";
	}
	else
	{
		strMessage = L"Are you sure you want to permanently delete the "
			L"folder '";
		strMessage += bstrName;
		strMessage += L"' and all of its contents?";
	}

	int ret = ::IsolationAwareMessageBox(hwnd, strMessage,
		(fIsFolder) ?
			L"Confirm Folder Delete" : L"Confirm File Delete", 
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);

	return (ret == IDYES);
}

/**
 * Displays dialog seeking confirmation from user to delete multiple items.
 *
 * @param hwnd    Handle to the window used for UI.
 * @param cItems  Number of items selected for deletion.
 *
 * @returns  Whether confirmation was given or denied.
 */
bool CRemoteFolder::_ConfirmMultiDelete( HWND hwnd, size_t cItems )
{
	if (hwnd == NULL)
		return false;

	CString strMessage;
	strMessage.Format(
		L"Are you sure you want to permanently delete these %d items?", cItems);

	int ret = ::IsolationAwareMessageBox(hwnd, strMessage,
		L"Confirm Multiple Item Delete",
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);

	return (ret == IDYES);
}

/**
 * Creates an SFTP connection.
 *
 * The connection is created from the information stored in this
 * folder's PIDL, @c m_pidl, and the window handle to be used as the owner
 * window for any user interaction. This window handle can be NULL but (in order
 * to enforce good UI etiquette - we shouldn't attempt to interact with the user
 * if Explorer isn't expecting us to) any operation which requires user 
 * interaction should quietly fail.  
 *
 * @param hwndUserInteraction  A handle to the window which should be used
 *                             as the parent window for any user interaction.
 * @throws ATL exceptions on failure.
 */
com_ptr<ISftpProvider> CRemoteFolder::_CreateConnectionForFolder(
	HWND hwndUserInteraction)
{
	// Create SFTP Consumer for this HWNDs lifetime
	m_consumer = m_consumer_factory(hwndUserInteraction);

	return connection_from_pidl(root_pidl(), hwndUserInteraction);
}
