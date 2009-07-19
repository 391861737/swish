/**
    @file

    Fixtures common to several test cases that use Boost.Test.

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

#pragma once

#include "swish/boost_process.hpp"
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <string>

namespace test {
namespace common_boost {

/**
 * Fixture that initialises and uninitialises COM.
 */
template<typename T>
class ComFixture : public T
{
public:
	ComFixture()
	{
		HRESULT hr = ::CoInitialize(NULL);
		BOOST_WARN_EQUAL(hr, S_OK);
	}

	~ComFixture()
	{
		::CoUninitialize();
	}
};

/**
 * Fixture that starts and stops a local OpenSSH server instance.
 */
class OpenSshFixture
{
public:
	OpenSshFixture();
	~OpenSshFixture();

	int StopServer();

	std::string GetHost() const;
	int GetPort() const;
	boost::filesystem::path GetPrivateKey() const;
	boost::filesystem::path GetPublicKey() const;
	std::string ToRemotePath(
		boost::filesystem::path local_path) const;

private:
	boost::process::child m_sshd;
};

}} // namespace test::common_boost
