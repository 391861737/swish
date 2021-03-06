// Copyright 2010, 2012, 2013, 2015, 2016 Alexander Lamaison

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "swish/provider/sftp_provider.hpp" // sftp_provider, listing

#include "test/common_boost/helpers.hpp"
#include "test/fixtures/provider_fixture.hpp"
#include "test/fixtures/sftp_fixture.hpp"

#include <comet/datetime.h> // datetime_t, timeperiod_t
#include <comet/error.h>    // com_error
#include <comet/ptr.h>      // com_ptr

#include <ssh/filesystem/path.hpp>
#include <ssh/stream.hpp>

#include <boost/foreach.hpp> // BOOST_FOREACH
#include <boost/range/empty.hpp>
#include <boost/range/size.hpp>
#include <boost/test/unit_test.hpp>

#include <exception>
#include <string>
#include <vector>

using test::MockConsumer;
using test::fixtures::provider_fixture;
using test::fixtures::sftp_fixture;

using swish::provider::sftp_filesystem_item;
using swish::provider::sftp_provider;
using swish::provider::directory_listing;

using comet::com_error;
using comet::com_error_from_interface;
using comet::com_ptr;
using comet::datetime_t;
using comet::timeperiod_t;

using ssh::filesystem::ofstream;
using ssh::filesystem::path;

using boost::empty;
using boost::shared_ptr;
using boost::test_tools::predicate_result;

using std::exception;
using std::string;
using std::vector;
using std::wstring;

namespace
{

const string longentry =
    "-rw-r--r--    1 swish    wheel         767 Dec  8  2005 .cshrc";

predicate_result file_exists_in_listing(const path& filename,
                                        const directory_listing& listing)
{
    if (empty(listing))
    {
        predicate_result res(false);
        res.message() << "Enumerator is empty";
        return res;
    }
    else
    {
        BOOST_FOREACH (const sftp_filesystem_item& entry, listing)
        {
            if (filename.wstring() == entry.filename())
            {
                predicate_result res(true);
                res.message() << "File found in enumerator: " << filename;
                return res;
            }
        }

        predicate_result res(false);
        res.message() << "File not in enumerator: " << filename;
        return res;
    }
}

wstring filename_getter(const directory_listing::value_type& directory_entry)
{
    return directory_entry.filename().wstring();
}
}

BOOST_FIXTURE_TEST_SUITE(provider_tests, provider_fixture)

BOOST_AUTO_TEST_SUITE(listing_tests)

/**
 * List contents of an empty directory.
 */
BOOST_AUTO_TEST_CASE(list_empty_dir)
{
    shared_ptr<sftp_provider> provider = Provider();
    BOOST_CHECK_EQUAL(boost::size(provider->listing(sandbox())), 0U);
}

/**
 * List contents of a directory.
 */
BOOST_AUTO_TEST_CASE(list_dir)
{
    path file1 = new_file_in_sandbox();
    path file2 = new_file_in_sandbox();

    directory_listing listing = Provider()->listing(sandbox());

    BOOST_CHECK_EQUAL(boost::size(listing), 2U);

    vector<wstring> files;
    transform(listing.begin(), listing.end(), back_inserter(files),
              filename_getter);
    sort(files.begin(), files.end());

    // . and .. are NOT allowed in the listing
    vector<wstring> expected;
    expected.push_back(file1.filename().wstring());
    expected.push_back(file2.filename().wstring());
    sort(expected.begin(), expected.end());

    // Check format of listing is sensible
    BOOST_FOREACH (const sftp_filesystem_item& entry, listing)
    {
        wstring filename = entry.filename().wstring();

        BOOST_CHECK(!filename.empty());

        BOOST_CHECK(!entry.owner()->empty());
        BOOST_CHECK(!entry.group()->empty());

        // We don't know the exact date but check that it's very recent
        BOOST_CHECK(entry.last_modified().valid());
        BOOST_CHECK_GT(entry.last_modified(),
                       datetime_t::now_utc() -
                           timeperiod_t(0, 0, 0, 10)); // max 10 secs ago

        BOOST_CHECK(entry.last_accessed().valid());
        BOOST_CHECK_GT(entry.last_accessed(),
                       datetime_t::now_utc() -
                           timeperiod_t(0, 0, 0, 10)); // max 10 secs ago
    }
}

BOOST_AUTO_TEST_CASE(list_dir_many)
{
    // Fetch 5 listing enumerators
    vector<directory_listing> enums(5);

    BOOST_FOREACH (directory_listing& listing, enums)
    {
        listing = Provider()->listing(sandbox());
    }
}

BOOST_AUTO_TEST_CASE(listing_independence)
{
    // Put some files in the test area

    path file1 = new_file_in_sandbox();
    path file2 = new_file_in_sandbox();
    path file3 = new_file_in_sandbox();

    // Fetch first listing enumerator
    directory_listing listing_before = Provider()->listing(sandbox());

    // Delete one of the files
    remove(filesystem(), file2);

    // Fetch second listing enumerator
    directory_listing listing_after = Provider()->listing(sandbox());

    // The first listing should still show the file. The second should not.
    BOOST_CHECK(file_exists_in_listing(file1.filename(), listing_before));
    BOOST_CHECK(file_exists_in_listing(file2.filename(), listing_before));
    BOOST_CHECK(file_exists_in_listing(file3.filename(), listing_before));
    BOOST_CHECK(file_exists_in_listing(file1.filename(), listing_after));
    BOOST_CHECK(!file_exists_in_listing(file2.filename(), listing_after));
    BOOST_CHECK(file_exists_in_listing(file3.filename(), listing_after));
}

namespace
{

predicate_result is_failed_to_open(const exception& e)
{
    string expected = "Failed opening remote file: FX_NO_SUCH_FILE";
    string actual = e.what();

    if (expected != actual)
    {
        predicate_result res(false);
        res.message() << "Exception is not failure to open [" << expected
                      << " != " << actual << "]";
        return res;
    }

    return true;
}
}

/**
 * Try to list non-existent directory.
 */
BOOST_AUTO_TEST_CASE(list_dir_error)
{
    shared_ptr<sftp_provider> provider = Provider();

    BOOST_CHECK_EXCEPTION(provider->listing("/i/dont/exist"), exception,
                          is_failed_to_open);
}

/**
 * Can we handle a unicode filename?
 */
BOOST_AUTO_TEST_CASE(unicode)
{
    // create an empty file with a unicode filename in the sandbox
    path unicode_file_name = new_file_in_sandbox(L"русский");

    directory_listing listing = Provider()->listing(sandbox());

    BOOST_CHECK_EQUAL(listing[0].filename().wstring(),
                      unicode_file_name.filename().wstring());
}

/**
 * Can we see inside directories whose names are non-latin Unicode?
 */
BOOST_AUTO_TEST_CASE(list_unicode_dir)
{
    path directory = new_directory_in_sandbox(L"漢字 العربية русский 47");
    path file = directory / L"latin filename";
    ofstream(filesystem(), file).close();

    Provider()->listing(directory);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(renaming_tests)

BOOST_AUTO_TEST_CASE(rename_file)
{
    path file = new_file_in_sandbox();
    path renamed_file =
        file.parent_path() / (file.filename().wstring() + L"renamed");

    shared_ptr<sftp_provider> provider = Provider();

    ssh::filesystem::path old_name(file);
    ssh::filesystem::path new_name(renamed_file);

    BOOST_CHECK_EQUAL(provider->rename(Consumer().get(), old_name, new_name),
                      VARIANT_FALSE);
    BOOST_CHECK(exists(filesystem(), renamed_file));
    BOOST_CHECK(!exists(filesystem(), file));

    // Rename back
    BOOST_CHECK_EQUAL(provider->rename(Consumer().get(), new_name, old_name),
                      VARIANT_FALSE);
    BOOST_CHECK(!exists(filesystem(), renamed_file));
    BOOST_CHECK(exists(filesystem(), file));
}

BOOST_AUTO_TEST_CASE(rename_unicode_file)
{
    // create an empty file with a unicode filename in the sandbox
    path unicode_file_name = new_file_in_sandbox(L"русский.txt");

    path renamed_file = sandbox() / L"Россия";

    shared_ptr<sftp_provider> provider = Provider();

    ssh::filesystem::path old_name(unicode_file_name);
    ssh::filesystem::path new_name(renamed_file);

    BOOST_CHECK_EQUAL(provider->rename(Consumer().get(), old_name, new_name),
                      VARIANT_FALSE);

    BOOST_CHECK(exists(filesystem(), renamed_file));
    BOOST_CHECK(!exists(filesystem(), unicode_file_name));
}

/**
 * Test that we prompt the user to confirm overwrite and that we
 * perform the overwrite correctly because the user approved to operation.
 */
BOOST_AUTO_TEST_CASE(rename_with_obstruction)
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::AllowOverwrite);

    path subject = new_file_in_sandbox();

    // Obstruct renaming by creating an empty file on the target location
    path target =
        new_file_in_sandbox(subject.filename().wstring() + L"renamed");

    // Swish creates a temporary for non-atomic overwrite to minimise the
    // chance of failing to rename but losing the overwritten file as well.
    // We need to check this gets removed correctly.
    path swish_rename_temp_file =
        target.parent_path() /
        (target.filename().wstring() + L".swish_rename_temp");

    // Check that the non-atomic overwrite temp does not already exists
    BOOST_CHECK(!exists(filesystem(), swish_rename_temp_file));

    BOOST_CHECK_EQUAL(Provider()->rename(consumer.in(), subject, target),
                      VARIANT_TRUE);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that the old file no longer exists but the target does
    BOOST_CHECK(!exists(filesystem(), subject));
    BOOST_CHECK(exists(filesystem(), target));

    // Check that the non-atomic overwrite temp has been removed
    BOOST_CHECK(!exists(filesystem(), swish_rename_temp_file));
}

namespace
{

bool is_abort(const com_error& error)
{
    return error.hr() == E_ABORT;
}
}

/**
 * Test that we prompt the user to confirm overwrite and that we
 * do not perform the overwrite because the user denied permission.
 *
 * TODO: check the contents of the target file to make sure it is untouched.
 */
BOOST_AUTO_TEST_CASE(rename_with_obstruction_refused_overwrite_permission)
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::PreventOverwrite);

    path subject = new_file_in_sandbox();
    // Obstruct renaming by creating an empty file on the target location
    path target =
        new_file_in_sandbox(subject.filename().wstring() + L"renamed");

    BOOST_CHECK_EXCEPTION(Provider()->rename(consumer.in(), subject, target),
                          com_error, is_abort);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that both files still exist
    BOOST_CHECK(exists(filesystem(), subject));
    BOOST_CHECK(exists(filesystem(), target));
}

/*
 * The next three tests just duplicate the ones above but for directories
 * instead of files.
 */

BOOST_AUTO_TEST_CASE(rename_directory)
{
    path subject = new_directory_in_sandbox();
    path target =
        subject.parent_path() / (subject.filename().wstring() + L"renamed");

    shared_ptr<sftp_provider> provider = Provider();

    ssh::filesystem::path old_name(subject);
    ssh::filesystem::path new_name(target);

    BOOST_CHECK_EQUAL(provider->rename(Consumer().get(), old_name, new_name),
                      VARIANT_FALSE);
    BOOST_CHECK(exists(filesystem(), target));
    BOOST_CHECK(is_directory(filesystem(), target));
    BOOST_CHECK(!exists(filesystem(), subject));

    // Rename back
    BOOST_CHECK_EQUAL(provider->rename(Consumer().get(), new_name, old_name),
                      VARIANT_FALSE);
    BOOST_CHECK(!exists(filesystem(), target));
    BOOST_CHECK(exists(filesystem(), subject));
    BOOST_CHECK(is_directory(filesystem(), subject));
}

/**
 * This differs from the file version of the test in that obstructing
 * directories are harder to delete because they may have contents.
 * This test exercises that harder situation by adding a file to the
 * obstructing directory.
 *
 * TODO: Check the subject directory contents remains after renaming.
 */
BOOST_AUTO_TEST_CASE(rename_directory_with_obstruction)
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::AllowOverwrite);

    path subject = new_directory_in_sandbox();

    // Obstruct renaming by creating an empty file on the target location
    path target =
        new_directory_in_sandbox(subject.filename().wstring() + L"renamed");

    // Swish creates a temporary for non-atomic overwrite to minimise the
    // chance of failing to rename but losing the overwritten file as well.
    // We need to check this gets removed correctly.
    path swish_rename_temp_file =
        target.parent_path() /
        (target.filename().wstring() + L".swish_rename_temp");

    // Check that the non-atomic overwrite temp does not already exist
    BOOST_CHECK(!exists(filesystem(), swish_rename_temp_file));

    // Add a file in the obstructing directory to make it harder to delete
    path target_contents = target / L"somefile";
    ofstream(filesystem(), target_contents).close();

    BOOST_CHECK_EQUAL(Provider()->rename(consumer.in(), subject, target),
                      VARIANT_TRUE);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that the old file no longer exists but the target does
    BOOST_CHECK(!exists(filesystem(), subject));
    BOOST_CHECK(exists(filesystem(), target));

    // Check that the non-atomic overwrite temp has been removed
    BOOST_CHECK(!exists(filesystem(), swish_rename_temp_file));
}

/**
 * TODO: check the contents of the target directory to make sure it's untouched.
 */
BOOST_AUTO_TEST_CASE(
    rename_directory_with_obstruction_refused_overwrite_permission)
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::PreventOverwrite);

    path subject = new_directory_in_sandbox();
    // Obstruct renaming by creating an empty file on the target location
    path target =
        new_directory_in_sandbox(subject.filename().wstring() + L"renamed");

    BOOST_CHECK_EXCEPTION(Provider()->rename(consumer.in(), subject, target),
                          com_error, is_abort);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that both files still exist
    BOOST_CHECK(exists(filesystem(), subject));
    BOOST_CHECK(exists(filesystem(), target));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(deleting_tests)

/**
 * Delete a file and ensure other files in the same folder aren't also removed.
 */
BOOST_AUTO_TEST_CASE(delete_file)
{
    path file_before = new_file_in_sandbox();
    path file = new_file_in_sandbox();
    path file_after = new_file_in_sandbox();

    Provider()->remove_all(file);

    BOOST_CHECK(exists(filesystem(), file_before));
    BOOST_CHECK(!exists(filesystem(), file));
    BOOST_CHECK(exists(filesystem(), file_after));
}

/**
 * Delete a file with a unicode filename.
 */
BOOST_AUTO_TEST_CASE(delete_unicode_file)
{
    path unicode_file_name = new_file_in_sandbox(L"العربية.txt");

    Provider()->remove_all(unicode_file_name);

    BOOST_CHECK(!exists(filesystem(), unicode_file_name));
}

/**
 * Delete an empty directory.
 */
BOOST_AUTO_TEST_CASE(delete_empty_directory)
{
    path directory = sandbox() / L"العربية";
    create_directory(filesystem(), directory);

    Provider()->remove_all(directory);

    BOOST_CHECK(!exists(filesystem(), directory));
}

/**
 * Delete a non-empty directory.  This is trickier as the contents have to be
 * deleted before the directory.
 */
BOOST_AUTO_TEST_CASE(delete_directory_recursively)
{
    path directory = new_directory_in_sandbox(L"العربية");
    BOOST_CHECK(exists(filesystem(), directory));

    path file = directory / L"русский.txt";
    ofstream(filesystem(), file).close();

    BOOST_CHECK(exists(filesystem(), file));

    Provider()->remove_all(directory);

    BOOST_CHECK(!exists(filesystem(), directory));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(file_creation_tests)

/**
 * Create a directory with a unicode filename.
 */
BOOST_AUTO_TEST_CASE(create_directory)
{
    path file = sandbox() / L"漢字 العربية русский 47";
    BOOST_CHECK(!exists(filesystem(), file));

    Provider()->create_new_directory(file);

    BOOST_CHECK(exists(filesystem(), file));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(stream_creation_tests)

/**
 * Create a stream to a file with a unicode filename.
 *
 * Tests file creation as we don't create the file before the call and we
 * check that it exists after.
 */
BOOST_AUTO_TEST_CASE(get_file_stream)
{
    path file = sandbox() / L"漢字 العربية русский.txt";
    BOOST_CHECK(!exists(filesystem(), file));

    com_ptr<IStream> stream = Provider()->get_file(file, std::ios_base::out);

    BOOST_CHECK(stream);
    BOOST_CHECK(exists(filesystem(), file));

    STATSTG statstg = STATSTG();
    HRESULT hr = stream->Stat(&statstg, STATFLAG_DEFAULT);
    BOOST_CHECK_EQUAL(statstg.pwcsName, file.filename());
    ::CoTaskMemFree(statstg.pwcsName);
    BOOST_REQUIRE_OK(hr);
}

/**
 * Try to get a read-only stream to a non-existent file.
 *
 * This must fail as out DropTarget uses it to check whether the file already
 * exists.
 */
BOOST_AUTO_TEST_CASE(get_file_stream_fail)
{
    path file = sandbox() / L"漢字 العربية русский.txt";
    BOOST_CHECK(!exists(filesystem(), file));

    BOOST_CHECK_THROW(Provider()->get_file(file, std::ios_base::in), exception);

    BOOST_CHECK(!exists(filesystem(), file));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
