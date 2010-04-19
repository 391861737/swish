/**
    @file

    Expose the remote filesystem as an IDropTarget.

    @if licence

    Copyright (C) 2009, 2010  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "DropTarget.hpp"

#include "data_object/ShellDataObject.hpp"  // ShellDataObject
#include "shell.hpp"  // bind_to_handler_object, strret_to_string
#include "swish/catch_com.hpp"  // catchCom
#include "swish/exception.hpp"  // com_exception
#include "swish/windows_api.hpp" // SHBindToParent
#include "swish/interfaces/SftpProvider.h" // ISftpProvider/Consumer

#include <boost/shared_ptr.hpp>  // shared_ptr
#include <boost/integer_traits.hpp>
#include <boost/locale.hpp> // translate
#include <boost/throw_exception.hpp>  // BOOST_THROW_EXCEPTION
#include <boost/system/system_error.hpp> // system_error

#include <comet/interface.h>  // uuidof, comtype
#include <comet/ptr.h>  // com_ptr
#include <comet/bstr.h> // bstr_t

#include <string>
#include <vector>

using swish::shell_folder::data_object::ShellDataObject;
using swish::shell_folder::data_object::PidlFormat;
using swish::shell_folder::bind_to_handler_object;
using swish::shell_folder::strret_to_string;
using swish::exception::com_exception;

using winapi::shell::pidl::pidl_t;
using winapi::shell::pidl::apidl_t;
using winapi::shell::pidl::cpidl_t;

using ATL::CComPtr;
using ATL::CComBSTR;

using boost::filesystem::wpath;
using boost::shared_ptr;
using boost::integer_traits;
using boost::locale::translate;
using boost::system::system_error;
using boost::system::system_category;

using comet::com_ptr;
using comet::uuidof;
using comet::bstr_t;

using std::wstring;
using std::vector;

namespace comet {

template<> struct comtype<IProgressDialog>
{
	static const IID& uuid() throw() { return IID_IProgressDialog; }
	typedef IUnknown base;
};

}

namespace { // private

	/**
	 * Given a DataObject and bitfield of allowed DROPEFFECTs, determine
	 * which drop effect, if any, should be chosen.  If none are
	 * appropriate, return DROPEFFECT_NONE.
	 */
	DWORD determine_drop_effect(
		const com_ptr<IDataObject>& pdo, DWORD allowed_effects)
	{
		if (pdo)
		{
			PidlFormat format(pdo);
			if (format.pidl_count() > 0)
			{
				if (allowed_effects & DROPEFFECT_COPY)
					return DROPEFFECT_COPY;
			}
		}

		return DROPEFFECT_NONE;
	}

	/**
	 * Given a PIDL to a *real* file in the filesystem, return an IStream 
	 * to it.
	 *
	 * @note  This fails with E_NOTIMPL on Windows 2000 and below.
	 */
	com_ptr<IStream> stream_from_shell_pidl(const apidl_t& pidl)
	{
		PCUITEMID_CHILD pidl_child;
		HRESULT hr;
		com_ptr<IShellFolder> folder;
		
		hr = swish::windows_api::SHBindToParent(
			pidl.get(), __uuidof(IShellFolder), 
			reinterpret_cast<void**>(folder.out()),
			&pidl_child);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		com_ptr<IStream> stream;
		
		hr = folder->BindToObject(
			pidl_child, NULL, __uuidof(IStream),
			reinterpret_cast<void**>(stream.out()));
		if (FAILED(hr))
		{
			hr = folder->BindToStorage(
				pidl_child, NULL, __uuidof(IStream),
				reinterpret_cast<void**>(stream.out()));
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
		}

		return stream;
	}

	/**
	 * Return the stream name from an IStream.
	 */
	wpath filename_from_stream(const com_ptr<IStream>& stream)
	{
		STATSTG statstg;
		HRESULT hr = stream->Stat(&statstg, STATFLAG_DEFAULT);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		shared_ptr<OLECHAR> name(statstg.pwcsName, ::CoTaskMemFree);
		return name.get();
	}

	/**
	 * Query an item's parent folder for the item's display name relative
	 * to that folder.
	 */
	wstring display_name_of_item(
		const com_ptr<IShellFolder>& parent_folder, const cpidl_t& pidl)
	{
		STRRET strret;
		HRESULT hr = parent_folder->GetDisplayNameOf(
			pidl.get(), SHGDN_INFOLDER | SHGDN_FORPARSING, &strret);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		return strret_to_string(strret, pidl);
	}

	/**
	 * Return the parsing name of an item.
	 */
	wpath display_name_from_pidl(const apidl_t& parent, const cpidl_t& item)
	{
		com_ptr<IShellFolder> parent_folder = 
			bind_to_handler_object<IShellFolder>(parent);

		return display_name_of_item(parent_folder, item);
	}

	/**
	 * Return the parsing path name for a PIDL relative the the given parent.
	 */
	wpath parsing_path_from_pidl(const apidl_t& parent, const pidl_t& pidl)
	{
		if (pidl.empty())
			return wpath();

		cpidl_t item;
		item.attach(::ILCloneFirst(pidl.get()));

		return display_name_from_pidl(parent, item) / 
			parsing_path_from_pidl(parent + item, ::ILNext(pidl.get()));
	}

	const size_t COPY_CHUNK_SIZE = 1024 * 32;

	template<typename Predicate>
	void copy_stream_to_remote_destination(
		const com_ptr<IStream>& local_stream, 
		const com_ptr<ISftpProvider>& provider,
		const com_ptr<ISftpConsumer>& consumer, wpath destination,
		Predicate cancelled)
	{
		CComBSTR bstrPath = destination.string().c_str();

		com_ptr<IStream> remote_stream;
		HRESULT hr = provider->GetFile(
			consumer.get(), bstrPath, true, remote_stream.out());
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		// Set both streams back to the start
		LARGE_INTEGER move = {0};
		hr = local_stream->Seek(move, SEEK_SET, NULL);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		hr = remote_stream->Seek(move, SEEK_SET, NULL);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		// Do the copy in chunks allowing us to cancel the operation
		ULARGE_INTEGER cb;
		cb.QuadPart = COPY_CHUNK_SIZE;
		while (!cancelled())
		{
			ULARGE_INTEGER cbRead = {0};
			ULARGE_INTEGER cbWritten = {0};
			// TODO: make our own CopyTo that propagates errors
			hr = local_stream->CopyTo(
				remote_stream.get(), cb, &cbRead, &cbWritten);
			assert(FAILED(hr) || cbRead.QuadPart == cbWritten.QuadPart);
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
			if (cbRead.QuadPart == 0)
				break; // finished
		}
	}

	/**
	 * Predicate functor checking status of progress dialogue.
	 */
	class cancel_check
	{
	public:
		cancel_check(const com_ptr<IProgressDialog>& dialog)
			: m_dialog(dialog) {}

		/**
		 * Returns if the user cancelled the operation.
		 */
		bool operator()() { return m_dialog && m_dialog->HasUserCancelled(); }

	private:
		com_ptr<IProgressDialog> m_dialog;
	};

	void create_remote_directory(
		const com_ptr<ISftpProvider>& provider, 
		const com_ptr<ISftpConsumer>& consumer, const wpath& remote_path)
	{
		bstr_t path = remote_path.string();

		HRESULT hr = provider->CreateNewDirectory(
			consumer.get(), path.get_raw());
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));
	}

	/**
	 * Storage structure for an item in the copy list built by 
	 * build_copy_list().
	 */
	struct CopylistEntry
	{
		CopylistEntry(
			const pidl_t& pidl, wpath relative_path, bool is_folder)
		{
			this->pidl = pidl;
			this->relative_path = relative_path;
			this->is_folder = is_folder;
		}

		pidl_t pidl;
		wpath relative_path;
		bool is_folder;
	};


	void build_copy_list_recursively(
		const apidl_t& parent, const pidl_t& folder_pidl,
		vector<CopylistEntry>& copy_list_out)
	{
		wpath folder_path = parsing_path_from_pidl(parent, folder_pidl);

		copy_list_out.push_back(
			CopylistEntry(folder_pidl, folder_path, true));

		com_ptr<IShellFolder> folder = 
			bind_to_handler_object<IShellFolder>(parent + folder_pidl);

		// Add non-folder contents

		com_ptr<IEnumIDList> e;
		HRESULT hr = folder->EnumObjects(
			NULL, SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, e.out());
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		cpidl_t item;
		while (hr == S_OK && e->Next(1, item.out(), NULL) == S_OK)
		{
			pidl_t pidl = folder_pidl + item;
			copy_list_out.push_back(
				CopylistEntry(
					pidl, parsing_path_from_pidl(parent, pidl), false));
		}

		// Recursively add folders

		hr = folder->EnumObjects(
			NULL, SHCONTF_FOLDERS | SHCONTF_INCLUDEHIDDEN, e.out());
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));

		while (hr == S_OK && e->Next(1, item.out(), NULL) == S_OK)
		{
			pidl_t pidl = folder_pidl + item;
			build_copy_list_recursively(parent, pidl, copy_list_out);
		}
	}

	/**
	 * Expand the top-level PIDLs into a list of all items in the hierarchy.
	 */
	void build_copy_list(PidlFormat format, vector<CopylistEntry>& copy_list)
	{
		for (unsigned int i = 0; i < format.pidl_count(); ++i)
		{
			pidl_t pidl = format.relative_file(i);
			try
			{
				// Test if streamable
				com_ptr<IStream> stream;
				stream = stream_from_shell_pidl(format.file(i));
				
				CopylistEntry entry(
					pidl, filename_from_stream(stream), false);
				copy_list.push_back(entry);
			}
			catch (com_exception)
			{
				// Treating the item as something with an IStream has failed
				// Now we try to treat it as an IShellFolder and hope we
				// have more success

				build_copy_list_recursively(
					format.parent_folder(), pidl, copy_list);
			}
		}
	}


	/**
	 * Exception-safe lifetime manager for an IProgressDialog object.
	 *
	 * Calls StartProgressDialog when created and StopProgressDialog when
	 * destroyed.
	 */
	class AutoStartProgressDialog
	{
	public:
		AutoStartProgressDialog(
			const com_ptr<IProgressDialog>& progress, HWND hwnd, DWORD flags,
			const wstring& title)
			: m_progress(progress)
		{
			if (!m_progress) return;

			HRESULT hr;

			hr = m_progress->SetTitle(title.c_str());
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));

			hr = m_progress->StartProgressDialog(hwnd, NULL, flags, NULL);
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
		}

		~AutoStartProgressDialog()
		{
			if (m_progress)
				m_progress->StopProgressDialog();
		}

		/**
		 * Has the user cancelled the operation via the progress dialogue?
		 */
		bool user_cancelled() const
		{
			return m_progress && m_progress->HasUserCancelled();
		}

		/**
		 * Set the indexth line of the display to the given text.
		 */
		void line(DWORD index, const wstring& text) const
		{
			if (!m_progress) return;

			HRESULT hr = m_progress->SetLine(index, text.c_str(), FALSE, NULL);
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
		}

		/**
		 * Set the indexth line of the display to the given path.
		 *
		 * Uses the inbuilt path compression.
		 */
		void line_path(DWORD index, const wpath& path) const
		{
			if (!m_progress) return;

			HRESULT hr = m_progress->SetLine(
				index, path.string().c_str(), TRUE, NULL);
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
		}

		/**
		 * Update the indicator to show current progress level.
		 */
		void update(ULONGLONG so_far, ULONGLONG out_of)
		{
			if (!m_progress) return;

			HRESULT hr = m_progress->SetProgress64(so_far, out_of);
			if (FAILED(hr))
				BOOST_THROW_EXCEPTION(com_exception(hr));
		}

	private:
		// disable copying
		AutoStartProgressDialog(const AutoStartProgressDialog&);
		AutoStartProgressDialog& operator=(const AutoStartProgressDialog&);

		const com_ptr<IProgressDialog> m_progress;
	};
}

namespace swish {
namespace shell_folder {

/**
 * Copy the items in the DataObject to the remote target.
 *
 * @param format  IDataObject wrapper holding the items to be copied.
 * @param provider  SFTP connection to copy data over.
 * @param remote_path  Path on the target filesystem to copy items into.
 *                     This must be a path to a @b directory.
 * @param progress  Optional progress dialogue.
 */
void copy_format_to_provider(
	PidlFormat format, const com_ptr<ISftpProvider>& provider,
	const com_ptr<ISftpConsumer>& consumer, 
	wpath remote_path, const com_ptr<IProgressDialog>& progress)
{
	vector<CopylistEntry> copy_list;
	build_copy_list(format, copy_list);

	AutoStartProgressDialog auto_progress(
		progress, NULL, PROGDLG_AUTOTIME, translate("#Progress#Copying..."));

	for (unsigned int i = 0; i < copy_list.size(); ++i)
	{
		if (auto_progress.user_cancelled())
			BOOST_THROW_EXCEPTION(com_exception(E_ABORT));

		wpath from_path = copy_list[i].relative_path;
		wpath to_path = remote_path / copy_list[i].relative_path;

		if (copy_list[i].is_folder)
		{
			auto_progress.line_path(1, from_path);
			auto_progress.line_path(2, to_path);

			create_remote_directory(provider, consumer, to_path);
			
			auto_progress.update(i, copy_list.size());
		}
		else
		{
			com_ptr<IStream> stream;

			stream = stream_from_shell_pidl(
				format.parent_folder() + copy_list[i].pidl);

			auto_progress.line_path(1, from_path);
			auto_progress.line_path(2, to_path);

			copy_stream_to_remote_destination(
				stream, provider, consumer, to_path, cancel_check(progress));
			
			auto_progress.update(i, copy_list.size());
		}
	}
}

/**
 * Copy the items in the DataObject to the remote target.
 *
 * @param pdo  IDataObject holding the items to be copied.
 * @param pProvider  SFTP connection to copy data over.
 * @param remote_path  Path on the target filesystem to copy items into.
 *                     This must be a path to a @b directory.
 */
void copy_data_to_provider(
	const com_ptr<IDataObject>& data_object,
	const com_ptr<ISftpProvider>& provider, 
	const com_ptr<ISftpConsumer>& consumer, wpath remote_path,
	const com_ptr<IProgressDialog>& progress)
{
	ShellDataObject data(data_object.get());
	if (data.has_pidl_format())
	{
		copy_format_to_provider(
			PidlFormat(data_object), provider, consumer, remote_path,
			progress);
	}
	else
	{
		BOOST_THROW_EXCEPTION(com_exception(E_FAIL));
	}
}

/**
 * Create an instance of the DropTarget initialised with a data provider.
 */
/*static*/ com_ptr<IDropTarget> CDropTarget::Create(
	const com_ptr<ISftpProvider>& provider,
	const com_ptr<ISftpConsumer>& consumer, const wpath& remote_path,
	bool show_progress)
{
	com_ptr<CDropTarget> sp = sp->CreateCoObject();
	sp->m_provider = provider;
	sp->m_consumer = consumer;
	sp->m_remote_path = remote_path;
	sp->m_show_progress = show_progress;
	return sp;
}

CDropTarget::CDropTarget()
{
}

CDropTarget::~CDropTarget()
{
}

/**
 * Indicate whether the contents of the DataObject can be dropped on
 * this DropTarget.
 *
 * @todo  Take account of the key state.
 */
STDMETHODIMP CDropTarget::DragEnter( 
	IDataObject* pdo, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect)
{
	if (!pdwEffect)
		return E_INVALIDARG;

	try
	{
		m_data_object = pdo;

		*pdwEffect = determine_drop_effect(pdo, *pdwEffect);
	}
	catchCom()
	return S_OK;
}

/**
 * Refresh the choice drop effect for the last DataObject passed to DragEnter.
 * Although the DataObject will not have changed, the key state and allowed
 * effects bitfield may have.
 *
 * @todo  Take account of the key state.
 */
STDMETHODIMP CDropTarget::DragOver( 
	DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect)
{
	if (!pdwEffect)
		return E_INVALIDARG;

	try
	{
		*pdwEffect = determine_drop_effect(m_data_object, *pdwEffect);
	}
	catchCom()
	return S_OK;
}

/**
 * End the drag-and-drop loop for the current DataObject.
 */
STDMETHODIMP CDropTarget::DragLeave()
{
	try
	{
		m_data_object = NULL;
	}
	catchCom()
	return S_OK;
}

/**
 * Perform the drop operation by either copying or moving the data
 * in the DataObject to the remote target.
 *
 * @todo  Take account of the key state.
 */
STDMETHODIMP CDropTarget::Drop( 
	IDataObject* pdo, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect)
{
	if (!pdwEffect)
		return E_INVALIDARG;

	*pdwEffect = determine_drop_effect(pdo, *pdwEffect);
	m_data_object = pdo;

	try
	{
		if (pdo && *pdwEffect == DROPEFFECT_COPY)
		{
			if (m_show_progress)
			{
				com_ptr<IProgressDialog> progress(CLSID_ProgressDialog);
				copy_data_to_provider(
					pdo, m_provider, m_consumer, m_remote_path, progress);
			}
			else
				copy_data_to_provider(
					pdo, m_provider, m_consumer, m_remote_path, NULL);
		}
	}
	catchCom()

	m_data_object = NULL;
	return S_OK;
}

}} // namespace swish::shell_folder
