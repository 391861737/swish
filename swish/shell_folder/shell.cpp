/**
    @file

    Utility functions to work with the Windows Shell Namespace.

    @if license

    Copyright (C) 2009, 2011  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "shell.hpp"

#include <winapi/shell/shell.hpp> // strret_to_string, parsing_name_from_pidl

#include <comet/error.h> // com_error

#include <string>

#include <shlobj.h>  // SHGetDesktopFolder, SHILCreateFromPath, ILFree
#include <Winerror.h>  // FAILED

using winapi::shell::parsing_name_from_pidl;
using winapi::shell::strret_to_string;

using comet::com_error;
using comet::com_error_from_interface;
using comet::com_ptr;
using comet::uuidof;

using boost::filesystem::wpath;
using boost::filesystem::wdirectory_iterator;
using boost::shared_ptr;

using std::invalid_argument;
using std::wstring;
using std::vector;

namespace swish {
namespace shell_folder {

wpath path_from_pidl(PIDLIST_ABSOLUTE pidl)
{
	return parsing_name_from_pidl(pidl);
}

shared_ptr<ITEMIDLIST_ABSOLUTE> pidl_from_path(
	const wpath& filesystem_path)
{
	PIDLIST_ABSOLUTE pidl;
	HRESULT hr = ::SHILCreateFromPath(
		filesystem_path.file_string().c_str(), &pidl, NULL);
	if (FAILED(hr))
		BOOST_THROW_EXCEPTION(com_error(hr));

	return shared_ptr<ITEMIDLIST_ABSOLUTE>(pidl, ::ILFree);
}

com_ptr<IDataObject> data_object_for_file(const wpath& file)
{
	return data_object_for_files(&file, &file + 1);
}

com_ptr<IDataObject> data_object_for_directory(const wpath& directory)
{
	if (!is_directory(directory))
		BOOST_THROW_EXCEPTION(
			invalid_argument("The path must be to a directory."));

	return data_object_for_files(
		wdirectory_iterator(directory), wdirectory_iterator());
}


}} // namespace swish::shell_folder
