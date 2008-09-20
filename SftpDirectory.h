/*  Manage remote directory as a collection of PIDLs.

    Copyright (C) 2007, 2008  Alexander Lamaison <awl03@doc.ic.ac.uk>

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
#include "stdafx.h"
#include "resource.h"          // main symbols

#include "Pidl.h"              // PIDL wrapper class
#include "Connection.h"        // For SFTP Connection container
#include "RemotePidlManager.h" // To create REMOTEPIDLs

#include <vector>
using std::vector;


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

typedef CComObject<CComSTLCopyContainer< vector<CChildPidl> > > CComPidlHolder;

// CSftpDirectory
class CSftpDirectory
{
public:
	/**
	 * Creates and initialises directory instance. 
	 *
	 * @param conn          SFTP connection container.
	 * @param pszDirectory  Path of remote directory this object represents.
	 */
	CSftpDirectory( __in CConnection& conn, __in PCTSTR pszDirectory ) :
		m_connection(conn), // Trim any trailing slashes and append single slash
		m_strDirectory(CString(pszDirectory).TrimRight(_T('/'))+_T('/'))
	{}

	/**
	 * @param grfFlags      Flags specifying nature of files to fetch.
	 */
	HRESULT GetEnum(
		__deref_out IEnumIDList **ppEnumIDList,  __in SHCONTF grfFlags );
	bool Rename(
		__in PCUITEMID_CHILD pidlOldFile, __in PCTSTR pszNewFilename )
		throw(...);
	void Delete( __in PCUITEMID_CHILD pidlFile ) throw(...);


private:
	CConnection m_connection;
	CString m_strDirectory;

	CRemotePidlManager m_PidlManager;
	vector<CChildPidl> m_vecPidls; ///< Directory contents as PIDLs.

	HRESULT _Fetch( __in SHCONTF grfFlags );
};


/**
 * Copy-policy to manage copying and destruction of PITEMID_CHILD pidls.
 */
struct _CopyChildPidl
{
	static HRESULT copy(PITEMID_CHILD *ppidlCopy, const CChildPidl *ppidl)
	{
		try
		{
			*ppidlCopy = ppidl->CopyTo();
		}
		catchCom();

		return S_OK;
	}

	static HRESULT copy(PITEMID_CHILD *ppidlCopy, const PITEMID_CHILD *ppidl)
	{
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
