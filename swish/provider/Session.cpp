/**
    @file

    Libssh2 SSH and SFTP session management

    @if licence

    Copyright (C) 2008, 2009  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "Session.hpp"

#include "swish/remotelimits.h"
#include "swish/debug.hpp"        // Debug macros
#include "swish/utils.hpp" // WideStringToUtf8String

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <boost/asio/ip/tcp.hpp> // Boost sockets: only used for name resolving
#include <boost/lexical_cast.hpp> // lexical_cast: convert port num to string

#include <string>

using swish::utils::WideStringToUtf8String;

using boost::asio::ip::tcp;
using boost::asio::error::host_not_found;
using boost::system::system_error;
using boost::system::error_code;
using boost::lexical_cast;

using std::string;

CSession::CSession() : 
	m_pSession(NULL), m_io(0), m_socket(m_io), m_pSftpSession(NULL), 
	m_bConnected(false)
{
	_CreateSession();
	ATLASSUME(m_pSession);
}

CSession::~CSession()
{
	_DestroySftpChannel();
	_DestroySession();
}

CSession::operator LIBSSH2_SESSION*() const
{
	ATLASSUME(m_pSession);
	return m_pSession;
}

CSession::operator LIBSSH2_SFTP*() const
{
	ATLASSUME(m_pSftpSession);
	return m_pSftpSession;
}

void CSession::Connect(PCWSTR pwszHost, unsigned int uPort) throw(...)
{
	// Are we already connected?
	if (m_bConnected)
		return;
	
	// Connect to host over TCP/IP
	_OpenSocketToHost(pwszHost, uPort);

	// Start up libssh2 and trade welcome banners, exchange keys,
    // setup crypto, compression, and MAC layers
	ATLASSERT(m_socket.native() != INVALID_SOCKET);
	if (libssh2_session_startup(*this, static_cast<int>(m_socket.native())) != 0)
	{
#ifdef _DEBUG
		char *szError;
		int cchError;
		int rc = libssh2_session_last_error(*this, &szError, &cchError, false);
		ATLTRACE("libssh2_sftp_init failed (%d): %s", rc, szError);
#endif
		_ResetSession();
		_CloseSocketToHost();
	
		AtlThrow(E_FAIL); // Legal to fail here, e.g. server refuses banner/kex
	}
	
	// Tell libssh2 we are blocking
	libssh2_session_set_blocking(*this, 1);

	m_bConnected = true;
}

void CSession::Disconnect()
{
	if (!m_bConnected)
		return;

	libssh2_session_disconnect(m_pSession, "Swish says goodbye.");
	m_bConnected = false;
}

void CSession::StartSftp() throw(...)
{
	_CreateSftpChannel();
}


/*----------------------------------------------------------------------------*
 * Private methods
 *----------------------------------------------------------------------------*/

/**
 * Allocate a blocking LIBSSH2_SESSION instance.
 */
void CSession::_CreateSession() throw(...)
{
	// Create a session instance
	m_pSession = libssh2_session_init();
	ATLENSURE_THROW( m_pSession, E_FAIL );
}

/**
 * Free a LIBSSH2_SESSION instance.
 */
void CSession::_DestroySession() throw()
{
	ATLASSUME(m_pSession);
	if (m_pSession)	// dual of libssh2_session_init()
	{
		Disconnect();
		libssh2_session_free(m_pSession);
		m_pSession = NULL;
	}
}

/**
 * Destroy and recreate a LIBSSH2_SESSION instance.
 *
 * A session instance which has been used in a libssh2_session_startup call
 * cannot be reused safely.
 */
void CSession::_ResetSession() throw(...)
{
	_DestroySession();
	_DestroySftpChannel();
	_CreateSession();
}

/**
 * Start up an SFTP channel on this SSH session.
 */
void CSession::_CreateSftpChannel() throw(...)
{
	ATLASSUME(m_pSftpSession == NULL);

	if (libssh2_userauth_authenticated(*this) == 0)
		AtlThrow(E_UNEXPECTED); // We must be authenticated first

    m_pSftpSession = libssh2_sftp_init(*this); // Start up SFTP session
	if (!m_pSftpSession)
	{
#ifdef _DEBUG
		char *szError;
		int cchError;
		int rc = libssh2_session_last_error(*this, &szError, &cchError, false);
		ATLTRACE("libssh2_sftp_init failed (%d): %s", rc, szError);
#endif
		AtlThrow(E_FAIL);
	}
}

/**
 * Shut down the SFTP channel.
 */
void CSession::_DestroySftpChannel() throw()
{
	if (m_pSftpSession) // dual of libssh2_sftp_init()
	{
		ATLVERIFY( !libssh2_sftp_shutdown(*this) );
		m_pSftpSession = NULL;
	}
}

/**
 * Creates a socket and connects it to the host.
 *
 * The socket is stored as the member variable @c m_socket. The hostname 
 * and port used are passed as parameters.
 *
 * @throws  A com_exception or boost::system::system_error if there is a
 *          failure.
 *
 * @remarks The socket should be cleaned up when no longer needed using
 *          @c _CloseSocketToHost()
 */
void CSession::_OpenSocketToHost(PCWSTR pwszHost, unsigned int uPort)
{
	ATLASSERT(pwszHost[0] != '\0');
	ATLASSERT(uPort >= MIN_PORT && uPort <= MAX_PORT);

	// Convert host address to a UTF-8 string
	string host_name = WideStringToUtf8String(pwszHost);

	tcp::resolver resolver(m_io);
	tcp::endpoint endpoint;
	typedef tcp::resolver::query Lookup;
	Lookup query(
		endpoint.protocol(), host_name, lexical_cast<string>(uPort), 
		Lookup::all_matching | Lookup::numeric_service);

	tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
	tcp::resolver::iterator end;

	error_code error = host_not_found;
	while (error && endpoint_iterator != end)
	{
		m_socket.close();
		m_socket.connect(*endpoint_iterator++, error);
	}
	if (error)
		BOOST_THROW_EXCEPTION(system_error(error));
}

/**
 * Closes the socket stored in @c m_socket and sets is to @c INVALID_SOCKET.
 */
void CSession::_CloseSocketToHost() throw()
{
	m_socket.close();
}