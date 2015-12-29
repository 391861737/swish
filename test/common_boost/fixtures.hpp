/**
    @file

    Fixtures common to several test cases that use Boost.Test.

    @if license

    Copyright (C) 2009, 2012, 2013, 2014
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

#pragma once

#include <ssh/filesystem/path.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <boost/optional/optional.hpp>
#include <boost/process/child.hpp> // child process
#include <boost/system/system_error.hpp>  // For system_error

#include <Winsock2.h>  // For WSAStartup/Cleanup
#include <objbase.h>  // For CoInitialize/Uninitialize

#include <string>

namespace test {

/**
 * Fixture that initialises and uninitialises COM.
 */
class ComFixture
{
public:
    ComFixture()
    {
        HRESULT hr = ::CoInitialize(NULL);
        BOOST_WARN_MESSAGE(SUCCEEDED(hr), "::CoInitialize failed");
    }

    virtual ~ComFixture()
    {
        ::CoUninitialize();
    }
};

/**
 * Fixture that initialises and uninitialises Winsock.
 */
class WinsockFixture
{
public:
    WinsockFixture()
    {
        WSADATA wsadata;
        int err = ::WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (err)
            throw boost::system::system_error(
                err, boost::system::get_system_category());
    }

    virtual ~WinsockFixture()
    {
        ::WSACleanup();
    }
};

namespace detail {

    class OpenSshServer
    {
    public:
        explicit OpenSshServer(int port);
        ~OpenSshServer();
        int pid() const;

    private:
        OpenSshServer(const OpenSshServer&);
        OpenSshServer& operator=(const OpenSshServer&);

        boost::process::child m_sshd;
    };
}

/**
 * Fixture that starts and stops a local OpenSSH server instance.
 */
class OpenSshFixture : public WinsockFixture
{
public:
    OpenSshFixture();

    void restart_server();

    std::string GetHost() const;
    std::string GetUser() const;
    int GetPort() const;
    boost::filesystem::path PrivateKeyPath() const;
    boost::filesystem::path PublicKeyPath() const;
    ssh::filesystem::path ToRemotePath(
        boost::filesystem::path local_path) const;

private:
    int m_port;
    boost::optional<detail::OpenSshServer> m_openssh;
};

/**
 * Fixture that creates and destroys a sandbox directory.
 */
class SandboxFixture
{
public:
    SandboxFixture();
    virtual ~SandboxFixture();

    boost::filesystem::path Sandbox();
    boost::filesystem::path NewFileInSandbox();
    boost::filesystem::path NewFileInSandbox(std::wstring name);
    boost::filesystem::path NewDirectoryInSandbox();
    boost::filesystem::path NewDirectoryInSandbox(std::wstring name);

private:
    boost::filesystem::path m_sandbox;
};

} // namespace test
