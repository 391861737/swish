/**
    @file

    Externally COM-creatable aspects of Swish.

    @if licence

    Copyright (C) 2009  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "pch.h"

#include <HostFolder.h>
#include <RemoteFolder.h>

#include "resource.h"       // main symbols
#include "swish.h"          // Swish type-library

namespace swish {
namespace com_dll {

using namespace ATL;

/**
 * COM factory for externally created instances of CHostFolder.
 */
class ATL_NO_VTABLE CHostFolderCoClass :
	public CHostFolder,
	public CComCoClass<CHostFolderCoClass, &CLSID_CHostFolder>
{
public:

	DECLARE_REGISTRY_RESOURCEID(IDR_HOSTFOLDER)

	BEGIN_COM_MAP(CHostFolderCoClass)
		COM_INTERFACE_ENTRY(IShellFolder)
		COM_INTERFACE_ENTRY_CHAIN(CHostFolder)
	END_COM_MAP()
};

OBJECT_ENTRY_AUTO(__uuidof(CHostFolder), CHostFolderCoClass)

/**
 * COM factory for externally created instances of CRemoteFolder.
 */
class ATL_NO_VTABLE CRemoteFolderCoClass :
	public CRemoteFolder,
	public CComCoClass<CRemoteFolderCoClass, &CLSID_CRemoteFolder>
{
public:

	DECLARE_REGISTRY_RESOURCEID(IDR_REMOTEFOLDER)

	BEGIN_COM_MAP(CRemoteFolderCoClass)
		COM_INTERFACE_ENTRY(IShellFolder)
		COM_INTERFACE_ENTRY_CHAIN(CRemoteFolder)
	END_COM_MAP()
};

OBJECT_ENTRY_AUTO(__uuidof(CRemoteFolder), CRemoteFolderCoClass)

}} // namespace swish::com_dll