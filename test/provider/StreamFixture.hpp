/**
    @file

    Fixture for tests that need instances of CSftpStream.

    @if license

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

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.

    @endif
*/

#pragma once

#include "SftpFixture.hpp" // SftpFixture

#include "test/common_boost/ConsumerStub.hpp"  // CConsumerStub

#include "swish/provider/SessionFactory.hpp"  // CSessionFactory
#include "swish/utils.hpp"  // String conversion functions, GetCurrentUser

#include <boost/shared_ptr.hpp>  // shared_ptr

#include <memory>  // auto_ptr

namespace test {
namespace provider {

/**
 * Extends the Sandbox fixture by allowing the creation of swish::provider
 * IStreams that pass through the OpenSSH server pointing to files in the
 * sandbox.
 */
class StreamFixture : public test::provider::SftpFixture
{
public:

	boost::filesystem::wpath m_local_path;
	std::string m_remote_path;

	/**
	 * Initialise the test fixture with the path of a new, empty file
	 * in the sandbox.
	 */
	StreamFixture() 
		: m_local_path(NewFileInSandbox()), 
		  m_remote_path(ToRemotePath(
			swish::utils::WideStringToUtf8String(m_local_path.file_string())))
	{
	}

	/**
	 * Create an IStream instance open on a temporary file in our sandbox.
	 * By default the stream is open for reading and writing but different
	 * flags can be passed to change this.
	 */
	ATL::CComPtr<IStream> GetStream(
		CSftpStream::OpenFlags flags = CSftpStream::read | CSftpStream::write)
	{
		boost::shared_ptr<CSession> session(Session());

		ATL::CComPtr<IStream> stream = new CSftpStream(
			session, m_remote_path, flags);
		return stream;
	}
};

}} // namespace test::provider
