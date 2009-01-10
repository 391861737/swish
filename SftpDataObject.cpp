/*  DataObject creating FILE_DESCRIPTOR/FILE_CONTENTS formats from remote data.

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
*/

#include "stdafx.h"
#include "SftpDataObject.h"

#include "HostPidl.h"
#include "RemotePidl.h"
#include "SftpDirectory.h"

CSftpDataObject::CSftpDataObject() :
	m_fRenderedContents(false), m_fRenderedDescriptor(false)
{
}

CSftpDataObject::~CSftpDataObject()
{
}

/**
 * Initialise the DataObject with the top-level PIDLs.
 *
 * These PIDLs represent, for instance, the current group of files and
 * directories which have been selected in an Explorer window.  This list
 * should not include any sub-items of any of the directories.
 *
 * @param cPidl             Number of PIDLs in the selection.
 * @param aPidl             The selected PIDLs.
 * @param pidlCommonParent  PIDL to the common parent of all the PIDLs.
 * @param conn              SFTP connection to communicate with remote server.
 *
 * @throws  CAtlException on error.
 */
void CSftpDataObject::Initialize(
	UINT cPidl, PCUITEMID_CHILD_ARRAY aPidl, 
	PCIDLIST_ABSOLUTE pidlCommonParent, CConnection& conn)
throw(...)
{
	ATLENSURE_THROW(!m_spDoInner, E_UNEXPECTED); // Initialised twice

	// Initialise superclass which will create inner IDataObject
	__super::Initialize(cPidl, aPidl, pidlCommonParent);

	// Make a copy of the PIDLs.  These are used to delay-render the 
	// CFSTR_FILEDESCRIPTOR and CFSTR_FILECONTENTS format in GetData().
	m_pidlCommonParent = pidlCommonParent;
	std::copy(aPidl, aPidl + cPidl, std::back_inserter(m_pidls));

	// Prod the inner object with the formats whose data we will delay-
	// render in GetData()
	if (cPidl > 0)
	{
		HRESULT hr;
		hr = ProdInnerWithFormat(m_cfFileDescriptor);
		ATLENSURE_SUCCEEDED(hr);
		hr = ProdInnerWithFormat(m_cfFileContents);
		ATLENSURE_SUCCEEDED(hr);
	}

	// Save connection
	m_conn = conn;
}

/*----------------------------------------------------------------------------*
 * IDataObject methods
 *----------------------------------------------------------------------------*/

STDMETHODIMP CSftpDataObject::GetData(
	FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
	try
	{
		// Delay-render data if necessary
		if (pformatetcIn->cfFormat == m_cfFileDescriptor)
		{
			// Delay-render CFSTR_FILEDESCRIPTOR format into this IDataObject
			_DelayRenderCfFileGroupDescriptor();
		}
		else if (pformatetcIn->cfFormat == m_cfFileContents)
		{
			// Delay-render CFSTR_FILECONTENTS format into this IDataObject
			_DelayRenderCfFileContents();
		}

		// Delegate all requests to the superclass
		return __super::GetData(pformatetcIn, pmedium);
	}
	catchCom()
}

/*----------------------------------------------------------------------------*
 * Private methods
 *----------------------------------------------------------------------------*/

/**
 * Delay render CFSTR_FILEDESCRIPTOR format for PIDLs passed to Initialize().
 *
 * Unlike the CFSTR_SHELLIDLIST format, the file group descriptor should
 * include not only the top-level items but also any subitems within and
 * directories.  This enables Explorer to copy or move an entire directory
 * tree.
 *
 * As this operation can be very expensive when the directory tree is deep,
 * it isn't appropriate to do this when the IDataObject is created.  This
 * would lead to large delays when simply opening a directory---an operation
 * that also requires an IDataObject.  Instead, this format is delay-rendered
 * from the list of PIDLs cached during Initialize() the first time it is 
 * requested.
 *
 * @see _DelayRenderCfFileContents()
 *
 * @throws  CAtlException on error.
 */
void CSftpDataObject::_DelayRenderCfFileGroupDescriptor()
throw(...)
{
	if (!m_fRenderedDescriptor && !m_pidls.empty())
	{
		// Create FILEGROUPDESCRIPTOR format from the cached PIDL list
		CFileGroupDescriptor fgd = _CreateFileGroupDescriptor();
		ATLASSERT(fgd.GetSize() > 0);

		// Insert the descriptor into the IDataObject
		CFormatEtc fetc(m_cfFileDescriptor);
		STGMEDIUM stg;
		stg.tymed = TYMED_HGLOBAL;
		stg.hGlobal = fgd.Detach();
		stg.pUnkForRelease = NULL;
		HRESULT hr = SetData(&fetc, &stg, true);
		if (FAILED(hr))
		{
			::ReleaseStgMedium(&stg);
		}
		ATLENSURE_SUCCEEDED(hr);

		m_fRenderedDescriptor = true;
	}
}

/**
 * Delay render CFSTR_FILECONTENTS formats for PIDLs passed to Initialize().
 *
 * Unlike the CFSTR_SHELLIDLIST format, the file contents formats should
 * include not only the top-level items but also any subitems within and
 * directories.  This enables Explorer to copy or move an entire directory
 * tree.
 *
 * As this operation can be very expensive when the directory tree is deep,
 * it isn't appropriate to do this when the IDataObject is created.  This
 * would lead to large delays when simply opening a directory---an operation
 * that also requires an IDataObject.  Instead, these formats are 
 * delay-rendered from the list of PIDLs cached during Initialize() the 
 * first time it is requested.
 *
 * @see _DelayRenderCfFileGroupDescriptor()
 *
 * @throws  CAtlException on error.
 */
void CSftpDataObject::_DelayRenderCfFileContents()
throw(...)
{
	if (!m_fRenderedContents && !m_pidls.empty())
	{
		// Create IStreams from the cached PIDL list
		StreamList streams = _CreateFileContentsStreams();
		ATLASSERT(!streams.empty());

		// Create FILECONTENTS format from the list of streams
		for (UINT i = 0; i < streams.size(); i++)
		{
			// Insert the stream into the IDataObject
			CFormatEtc fetc(m_cfFileContents, TYMED_ISTREAM, i);
			STGMEDIUM stg;
			stg.tymed = TYMED_ISTREAM;
			stg.pstm = streams[i].Detach(); // Invalidates stored item
			stg.pUnkForRelease = NULL;
			HRESULT hr = SetData(&fetc, &stg, true);
			if (FAILED(hr))
			{
				::ReleaseStgMedium(&stg);
			}
			ATLENSURE_SUCCEEDED(hr);
		}

		m_fRenderedContents = true;
	}

	// If we fail inside the loop, anything we added in previous iterations
	// is still set in the IDataObject.  Does this matter?
}

/**
 * Create CFSTR_FILEDESCRIPTOR format from cached PIDLs.
 */
CFileGroupDescriptor CSftpDataObject::_CreateFileGroupDescriptor()
throw(...)
{
	ExpandedList pidls;

	for (UINT i = 0; i < m_pidls.size(); i++)
	{
		ExpandedList expandedPidls = _ExpandTopLevelPidl(m_pidls[i]);
		pidls.insert(pidls.end(), expandedPidls.begin(), expandedPidls.end());
	}

	CFileGroupDescriptor fgd(static_cast<UINT>(pidls.size()));

	for (UINT j = 0; j < pidls.size(); j++)
	{
		CFileDescriptor fd(pidls[j], pidls.size() > 1);
		fgd.SetDescriptor(j, fd);
	}

	ATLASSERT(pidls.size() == fgd.GetSize());
	return fgd;
}

/**
 * Create IStreams to use in CFSTR_FILECONTENTS formats from a 
 * collection of top-level PIDLs.
 */
CSftpDataObject::StreamList CSftpDataObject::_CreateFileContentsStreams()
throw(...)
{
	StreamList streams;

	for (UINT i = 0; i < m_pidls.size(); i++)
	{
		ExpandedList expandedPidls = _ExpandTopLevelPidl(m_pidls[i]);

		for (UINT j = 0; j < expandedPidls.size(); j++)
		{
			CSftpDirectory directory(
				CHostItemAbsolute(
					m_pidlCommonParent, expandedPidls[j].CopyParent()),
				m_conn);
			CRemoteItemHandle pidl = expandedPidls[j].GetLast();
			streams.push_back(directory.GetFile(pidl));
		}
	}

	return streams;
}

/**
 * Expand one of the selected PIDLs to include any descendents.
 *
 * If the given PIDL is a simple item, the returned list just contains this
 * PIDL.  However, if it a directory it will contain the PIDL followed by 
 * all the items in and below the directory.
 */
CSftpDataObject::ExpandedList CSftpDataObject::_ExpandTopLevelPidl(
	const TopLevelPidl& pidl)
throw(...)
{
	ExpandedList pidls;

	if (pidl.IsFolder())
	{
		CAbsolutePidl pidlFolder(m_pidlCommonParent, pidl);

		// Explode subfolder and add to lists
		CSftpDirectory subdirectory(pidlFolder, m_conn);
		vector<CRelativePidl> vecPidls = subdirectory.FlattenDirectoryTree();
		pidls.insert(pidls.end(), vecPidls.begin(), vecPidls.end());
	}
	else
	{
		// Add simple item - common case
		pidls.push_back(pidl);
	}

	return pidls;
}