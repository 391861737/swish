/*  Declaration of the libssh2-based SFTP provider component.

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

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.
*/

#pragma once

#include "stdafx.h"

#include "resource.h"                        // main symbols
#include "SessionFactory.h"                  // CSession

#include "swish/shell_folder/SftpProvider.h" // ISftpProvider & ISftpConsumer

#include <atlstr.h>                          // CString

#include <list>

class ATL_NO_VTABLE CLibssh2Provider :
	public ISftpProvider,
	public CComObjectRoot
{
public:

	BEGIN_COM_MAP(CLibssh2Provider)
		COM_INTERFACE_ENTRY(ISftpProvider)
	END_COM_MAP()

	/** @name ATL Constructors */
	// @{
	CLibssh2Provider();
	DECLARE_PROTECT_FINAL_CONSTRUCT();
	HRESULT FinalConstruct();
	void FinalRelease();
	// @}

	/** @name ISftpProvider methods */
	// @{
	IFACEMETHODIMP Initialize(
		__in ISftpConsumer *pConsumer,
		__in BSTR bstrUser, __in BSTR bstrHost, UINT uPort );
	IFACEMETHODIMP SwitchConsumer(
		__in ISftpConsumer *pConsumer );
	IFACEMETHODIMP GetListing(
		__in BSTR bstrDirectory, __out IEnumListing **ppEnum );
	IFACEMETHODIMP GetFile(
		__in BSTR bstrFilePath, __out IStream **ppStream );
	IFACEMETHODIMP Rename(
		__in BSTR bstrFromPath, __in BSTR bstrToPath,
		__deref_out VARIANT_BOOL *pfWasTargetOverwritten );
	IFACEMETHODIMP Delete(
		__in BSTR bstrPath );
	IFACEMETHODIMP DeleteDirectory(
		__in BSTR bstrPath );
	IFACEMETHODIMP CreateNewFile(
		__in BSTR bstrPath );
	IFACEMETHODIMP CreateNewDirectory(
		__in BSTR bstrPath );
	// @}

private:
	ISftpConsumer *m_pConsumer;    ///< Callback to consuming object
	BOOL m_fInitialized;           ///< Flag if Initialize() has been called
	std::auto_ptr<CSession> m_spSession;///< SSH/SFTP session
	CString m_strUser;             ///< Holds username for remote connection
	CString m_strHost;             ///< Hold name of remote host
	UINT m_uPort;                  ///< Holds remote port to connect to

	HRESULT _Connect();
	void _Disconnect();

	CString _GetLastErrorMessage();
	CString _GetSftpErrorMessage( ULONG uError );

	HRESULT _RenameSimple( __in_z const char* szFrom, __in_z const char* szTo );
	HRESULT _RenameRetryWithOverwrite(
		__in ULONG uPreviousError,
		__in_z const char* szFrom, __in_z const char* szTo, 
		__out CString& strError );
	HRESULT _RenameAtomicOverwrite(
		__in_z const char* szFrom, __in_z const char* szTo, 
		__out CString& strError );
	HRESULT _RenameNonAtomicOverwrite(
		const char* szFrom, const char* szTo, CString& strError );

	HRESULT _Delete(
		__in_z const char *szPath, __out CString& strError );
	HRESULT _DeleteDirectory(
		__in_z const char *szPath, __out CString& strError );
	HRESULT _DeleteRecursive(
		__in_z const char *szPath, __out CString& strError );
};

/**
 * A COM holder for an STL collection that can be used in an enumeration.
 * The enumerator (IEnumXXX) will take a pointer to this holder when it is
 * created which ensures that the STL collection lives at least as long as
 * the enumerator.
 */
template <typename CollType, typename ThreadingModel = CComObjectThreadModel>
class CComSTLCopyContainer :
	public CComObjectRootEx<ThreadingModel>,
	public IUnknown
{
public:
	HRESULT Copy(const CollType& coll)
	{
		try
		{
			m_coll = coll;
			return S_OK;
		}
		catch (...)
		{
			return E_OUTOFMEMORY;
		}
	}

BEGIN_COM_MAP(CComSTLCopyContainer)
	COM_INTERFACE_ENTRY(IUnknown)
END_COM_MAP()

	CollType m_coll;
};

typedef CComObject<CComSTLCopyContainer< std::list<Listing> > >
	CComListingHolder;


/**
 * Copy-policy for use by enumerators of Listing items.
 */
template<>
class _Copy<Listing>
{
public:
	static HRESULT copy(Listing* p1, const Listing* p2)
	{
		p1->bstrFilename = SysAllocStringLen(
			p2->bstrFilename, ::SysStringLen(p2->bstrFilename));
		p1->uPermissions = p2->uPermissions;
		p1->bstrOwner = SysAllocStringLen(
			p2->bstrOwner, ::SysStringLen(p2->bstrOwner));
		p1->bstrGroup = SysAllocStringLen(
			p2->bstrGroup, ::SysStringLen(p2->bstrGroup));
		p1->uUid = p2->uUid;
		p1->uGid = p2->uGid;
		p1->uSize = p2->uSize;
		p1->cHardLinks = p2->cHardLinks;
		p1->dateModified = p2->dateModified;
		p1->dateAccessed = p2->dateAccessed;

		return S_OK;
	}
	static void init(Listing* p)
	{
		::ZeroMemory(p, sizeof(Listing));
	}
	static void destroy(Listing* p)
	{
		::SysFreeString(p->bstrFilename);
		::SysFreeString(p->bstrOwner);
		::SysFreeString(p->bstrGroup);
		::ZeroMemory(p, sizeof(Listing));
	}
};

typedef CComEnumOnSTL<IEnumListing, &__uuidof(IEnumListing), Listing, 
	_Copy<Listing>, std::list<Listing> > CComEnumListing;
