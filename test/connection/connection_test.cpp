/**
    @file

    Tests for the pool of SFTP connections.

    @if license

    Copyright (C) 2009, 2010, 2011, 2013
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

#include "swish/connection/connection.hpp"  // Test subject
#include "swish/utils.hpp"

#include "test/common_boost/helpers.hpp"
#include "test/common_boost/fixtures.hpp"
#include "test/common_boost/ConsumerStub.hpp"

#include <comet/ptr.h>  // com_ptr
#include <comet/bstr.h> // bstr_t
#include <comet/util.h> // thread

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <boost/shared_ptr.hpp>  // shared_ptr
#include <boost/foreach.hpp>  // BOOST_FOREACH
#include <boost/exception/diagnostic_information.hpp> // diagnostic_information

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

using swish::provider::sftp_provider;
using swish::connection::connection_spec;
using swish::utils::Utf8StringToWideString;

using test::OpenSshFixture;
using test::CConsumerStub;

using comet::com_ptr;
using comet::bstr_t;
using comet::thread;

using boost::filesystem::path;
using boost::shared_ptr;
using boost::test_tools::predicate_result;

using std::exception;
using std::string;
using std::vector;
using std::wstring;

namespace { // private

    /**
     * Fixture that returns backend connections from the connection pool.
     */
    class PoolFixture : public OpenSshFixture
    {
    public:
        shared_ptr<sftp_provider> GetSession()
        {
            return get_connection().pooled_session();
        }

        connection_spec get_connection()
        {
            return connection_spec(
                Utf8StringToWideString(GetHost()), 
                Utf8StringToWideString(GetUser()), GetPort());
        }

        com_ptr<ISftpConsumer> Consumer()
        {
            com_ptr<CConsumerStub> consumer = new CConsumerStub(
                PrivateKeyPath(), PublicKeyPath());
            return consumer;
        }

        /**
         * Check that the given provider responds sensibly to a request.
         */
        predicate_result alive(shared_ptr<sftp_provider> provider)
        {
            try
            {
                provider->listing(Consumer(), L"/");

                predicate_result res(true);
                res.message() << "Provider seems to be alive";
                return res;
            }
            catch(const exception& e)
            {
                predicate_result res(false);
                res.message() << "Provider seems to be dead: " << e.what();
                return res;
            }
        }

    };
}

BOOST_FIXTURE_TEST_SUITE(pool_tests, PoolFixture)

/**
 * Test that a connection specification can create a session and report its
 * status correctly.
 */
BOOST_AUTO_TEST_CASE( connection_create_session )
{
    connection_spec connection(get_connection());

    BOOST_CHECK_EQUAL(
        connection.session_status(),
        connection_spec::session_status::not_running);

    shared_ptr<sftp_provider> provider = GetSession();

    BOOST_CHECK_EQUAL(
        connection.session_status(),
        connection_spec::session_status::running);

    BOOST_CHECK(alive(provider));
}

/**
 * Test that a connection specification can create a session and report its
 * status correctly. This test differs from above in that the connection spec
 * is created afresh before and after.
 */
BOOST_AUTO_TEST_CASE( connection_create_session_fresh )
{
    BOOST_CHECK_EQUAL(
        get_connection().session_status(),
        connection_spec::session_status::not_running);

    shared_ptr<sftp_provider> provider = GetSession();

    BOOST_CHECK_EQUAL(
        get_connection().session_status(),
        connection_spec::session_status::running);

    BOOST_CHECK(alive(provider));
}

/**
 * Test that an unrelated connection specification doesn't have its status
 * altered by creating a different session.
 */
BOOST_AUTO_TEST_CASE( connection_create_session_unrelated )
{
    connection_spec connection(L"Unrelated", L"Spec", 123);

    BOOST_CHECK_EQUAL(
        connection.session_status(),
        connection_spec::session_status::not_running);

    shared_ptr<sftp_provider> provider = GetSession();

    BOOST_CHECK_EQUAL(
        connection.session_status(),
        connection_spec::session_status::not_running);

    BOOST_CHECK(alive(provider));
}

/**
 * Test that an unrelated connection specification doesn't have its status
 * altered by creating a different session. This test differs from above
 * in that the connection spec is created afresh before and after.
 */
BOOST_AUTO_TEST_CASE( connection_create_session_unrelated_fresh )
{
    BOOST_CHECK_EQUAL(
        connection_spec(L"Unrelated", L"Spec", 123).session_status(),
        connection_spec::session_status::not_running);

    shared_ptr<sftp_provider> provider = GetSession();

    BOOST_CHECK_EQUAL(
        connection_spec(L"Unrelated", L"Spec", 123).session_status(),
        connection_spec::session_status::not_running);

    BOOST_CHECK(alive(provider));
}

/**
 * Test that a second call to GetSession() returns the same instance.
 */
BOOST_AUTO_TEST_CASE( twice )
{
    BOOST_CHECK_EQUAL(
        get_connection().session_status(),
        connection_spec::session_status::not_running);

    shared_ptr<sftp_provider> first_provider = GetSession();
    BOOST_CHECK(alive(first_provider));

    BOOST_CHECK_EQUAL(
        get_connection().session_status(),
        connection_spec::session_status::running);

    shared_ptr<sftp_provider> second_provider = GetSession();
    BOOST_CHECK(alive(second_provider));

    BOOST_CHECK_EQUAL(
        get_connection().session_status(),
        connection_spec::session_status::running);

    BOOST_CHECK(second_provider == first_provider);
}

const int THREAD_COUNT = 30;

template <typename T>
class use_session_thread : public thread
{
public:
    use_session_thread(T* fixture) : thread(), m_fixture(fixture) {}

private:
    DWORD thread_main()
    {
        try
        {
            {
                // This first call may or may not return running depending on
                // whether it is on the first thread scheduled, so we don't test
                // its value, just that it succeeds.
                m_fixture->get_connection().session_status();

                shared_ptr<sftp_provider> first_provider = 
                    m_fixture->GetSession();

                // However, by this point it *must* be running
                BOOST_CHECK_EQUAL(
                    m_fixture->get_connection().session_status(),
                    connection_spec::session_status::running);

                BOOST_CHECK(m_fixture->alive(first_provider));

                BOOST_CHECK_EQUAL(
                    m_fixture->get_connection().session_status(),
                    connection_spec::session_status::running);

                shared_ptr<sftp_provider> second_provider = 
                    m_fixture->GetSession();

                BOOST_CHECK_EQUAL(
                    m_fixture->get_connection().session_status(),
                    connection_spec::session_status::running);

                BOOST_CHECK(m_fixture->alive(second_provider));

                BOOST_CHECK(second_provider == first_provider);
            }
        }
        catch (const std::exception& e)
        {
            BOOST_MESSAGE(boost::diagnostic_information(e));
        }

        return 1;
    }

    T* m_fixture;
};

typedef use_session_thread<PoolFixture> test_thread;

/**
 * Retrieve and prod a session from many threads.
 */
BOOST_AUTO_TEST_CASE( threaded )
{
    vector<shared_ptr<test_thread> > threads(THREAD_COUNT);

    BOOST_FOREACH(shared_ptr<test_thread>& thread, threads)
    {
        thread = shared_ptr<test_thread>(new test_thread(this));
        thread->start();
    }

    BOOST_FOREACH(shared_ptr<test_thread>& thread, threads)
    {
        thread->wait();
    }
}

BOOST_AUTO_TEST_SUITE_END()
