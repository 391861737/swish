/**
    @file

    Unit tests for the CDropTarget implementation of IDropTarget.

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

#include "swish/atl.hpp"

#include "swish/shell_folder/DropTarget.hpp"  // Test subject
#include "swish/shell_folder/shell.hpp"  // shell helper functions

#include "test/shell_folder/ProviderFixture.hpp"  // ProviderFixture

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <shlobj.h>

#include <string>
#include <vector>
#include <iterator>
#include <algorithm>

using swish::shell_folder::CDropTarget;
using swish::shell_folder::copy_data_to_provider;
using swish::shell_folder::data_object_for_files;
using test::provider::ProviderFixture;
using comet::com_ptr;
using boost::filesystem::wpath;
using boost::filesystem::ofstream;
using boost::filesystem::ifstream;
using boost::test_tools::predicate_result;
using std::string;
using std::vector;
using std::istreambuf_iterator;

namespace { // private

	const string TEST_DATA = "Lorem ipsum dolor sit amet.\nbob\r\nsally";

	
	/**
	 * Write some data to a collection of local files and return them in
	 * a DataObject created by the shell.
	 * 
	 * The files must all be in the same filesystem folder.
	 */
	template<typename It>
	com_ptr<IDataObject> create_multifile_data_object(It begin, It end)
	{
		for_each(begin, end, fill_file);
		return data_object_for_files(begin, end);
	}

	/**
	 * Write some data to a local file and return it as a DataObject.
	 */
	com_ptr<IDataObject> create_data_object(const wpath& local)
	{
		return create_multifile_data_object(&local, &local + 1);
	}

	/**
	 * Fill a file with the test data.
	 */
	void fill_file(const wpath& file)
	{
		ofstream stream(file);
		stream << TEST_DATA;
	}
	
	/**
	 * Check if a file's contents is our test data.
	 */
	predicate_result file_contents_correct(const wpath& file)
	{
		ifstream stream(file);
		string contents = string(
			istreambuf_iterator<char>(stream),
			istreambuf_iterator<char>());
		BOOST_REQUIRE_EQUAL(contents, TEST_DATA);

		if (contents != TEST_DATA)
		{
			predicate_result res(false);
			res.message()
				<< "File contents is not as expected [" << contents
				<< " != " << TEST_DATA << "]";
			return res;
		}

		return true;
	}
}

#pragma region SFTP folder Drop Target tests
BOOST_FIXTURE_TEST_SUITE(drop_target_tests, ProviderFixture)

/**
 * Create an instance.
 */
BOOST_AUTO_TEST_CASE( create )
{
	com_ptr<ISftpProvider> provider = Provider();
	BOOST_REQUIRE(provider);

	com_ptr<IDropTarget> sp = CDropTarget::Create(
		provider.in(), ToRemotePath(Sandbox()));
	BOOST_REQUIRE(sp);
}

#pragma region DataObject copy tests
BOOST_FIXTURE_TEST_SUITE(drop_target_copy_tests, ProviderFixture)

/**
 * Copy single regular file.
 *
 * Test our ability to handle a DataObject produced by the shell for a 
 * single, regular file (real file in the filesystem).
 */
BOOST_AUTO_TEST_CASE( copy_single )
{
	wpath local = NewFileInSandbox();
	com_ptr<IDataObject> spdo = create_data_object(local);

	wpath destination = Sandbox() / L"copy-destination";
	create_directory(destination);

	copy_data_to_provider(spdo.in(), Provider(), ToRemotePath(destination));

	wpath expected = destination / local.filename();

	BOOST_REQUIRE(exists(expected));
	BOOST_REQUIRE(file_contents_correct(expected));
}

/**
 * Copy several regular files.
 *
 * Test our ability to handle a DataObject produced by the shell for
 * more than one regular file (real files in the filesystem).
 */
BOOST_AUTO_TEST_CASE( copy_many )
{
	vector<wpath> locals;
	locals.push_back(NewFileInSandbox());
	locals.push_back(NewFileInSandbox());
	locals.push_back(NewFileInSandbox());

	com_ptr<IDataObject> spdo = create_multifile_data_object(
		locals.begin(), locals.end());

	wpath destination = Sandbox() / L"copy-destination";
	create_directory(destination);

	copy_data_to_provider(spdo.in(), Provider(), ToRemotePath(destination));

	vector<wpath>::const_iterator it;
	for (it = locals.begin(); it != locals.end(); ++it)
	{
		wpath expected = destination / (*it).filename();

		BOOST_REQUIRE(exists(expected));
		BOOST_REQUIRE(file_contents_correct(expected));
	}
}

BOOST_AUTO_TEST_SUITE_END()
#pragma endregion

#pragma region Drag-n-Drop behaviour tests
BOOST_FIXTURE_TEST_SUITE(drop_target_dnd_tests, ProviderFixture)

/**
 * Drag enter.  
 * Simulate the user dragging a file onto our folder with the left 
 * mouse button.  The 'shell' is requesting either a copy or a link at our
 * discretion.  The folder drop target should respond S_OK and specify
 * that the effect of the operation is a copy.
 */
BOOST_AUTO_TEST_CASE( drag_enter )
{
	wpath local = NewFileInSandbox();
	com_ptr<IDataObject> spdo = create_data_object(local);

	com_ptr<IDropTarget> spdt = CDropTarget::Create(
		Provider(), ToRemotePath(Sandbox()));

	POINTL pt = {0, 0};
	DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_LINK;
	BOOST_REQUIRE_OK(spdt->DragEnter(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));
}

/**
 * Drag enter.  
 * Simulate the user dragging a file onto our folder but requesting an
 * effect, link, that we don't support.  The folder drop target should 
 * respond S_OK but set the effect to DROPEFFECT_NONE to indicate the drop
 * wasn't possible.
 */
BOOST_AUTO_TEST_CASE( drag_enter_bad_effect )
{
	wpath local = NewFileInSandbox();
	com_ptr<IDataObject> spdo = create_data_object(local);

	com_ptr<IDropTarget> spdt = CDropTarget::Create(
		Provider(), ToRemotePath(Sandbox()));

	POINTL pt = {0, 0};
	DWORD dwEffect = DROPEFFECT_LINK;
	BOOST_REQUIRE_OK(spdt->DragEnter(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_NONE));
}

/**
 * Drag over.  
 * Simulate the situation where a user drags a file over our folder and changes
 * which operation they want as they do so.  In other words, on DragEnter they
 * chose a link which we cannot perform but as they continue the drag (DragOver)
 * they chang their request to a copy which we can do.
 *
 * The folder drop target should respond S_OK and specify that the effect of 
 * the operation is a copy.
 */
BOOST_AUTO_TEST_CASE( drag_over )
{
	wpath local = NewFileInSandbox();
	com_ptr<IDataObject> spdo = create_data_object(local);

	com_ptr<IDropTarget> spdt = CDropTarget::Create(
		Provider(), ToRemotePath(Sandbox()));

	POINTL pt = {0, 0};

	// Do enter with link which should be declined (DROPEFFECT_NONE)
	DWORD dwEffect = DROPEFFECT_LINK;
	BOOST_REQUIRE_OK(spdt->DragEnter(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_NONE));

	// Change request to copy which sould be accepted
	dwEffect = DROPEFFECT_COPY;
	BOOST_REQUIRE_OK(spdt->DragOver(MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));
}

/**
 * Drag leave.  
 * Simulate an aborted drag-drop loop where the user drags a file onto our
 * folder, moves it around, and then leaves without dropping.
 *
 * The folder drop target should respond S_OK and any subsequent 
 * DragOver calls should be declined.
 */
BOOST_AUTO_TEST_CASE( drag_leave )
{
	wpath local = NewFileInSandbox();
	com_ptr<IDataObject> spdo = create_data_object(local);

	com_ptr<IDropTarget> spdt = CDropTarget::Create(
		Provider(), ToRemotePath(Sandbox()));

	POINTL pt = {0, 0};

	// Do enter with copy which sould be accepted
	DWORD dwEffect = DROPEFFECT_COPY;
	BOOST_REQUIRE_OK(spdt->DragEnter(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));

	// Continue drag
	BOOST_REQUIRE_OK(spdt->DragOver(MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));

	// Finish drag without dropping
	BOOST_REQUIRE_OK(spdt->DragLeave());

	// Decline any further queries until next DragEnter()
	BOOST_REQUIRE_OK(spdt->DragOver(MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_NONE));
}

/**
 * Drag and drop.
 * Simulate a complete drag-drop loop where the user drags a file onto our
 * folder, moves it around, and then drops it.
 *
 * The folder drop target should copy the contents of the DataObject to the
 * remote end, respond S_OK and any subsequent DragOver calls should be 
 * declined until a new drag-and-drop loop is started with DragEnter().
 *
 * @todo  Actually verify that the file ends up at the target end.
 */
BOOST_AUTO_TEST_CASE( drop )
{
	wpath local = NewFileInSandbox();
	wpath drop_target_directory = Sandbox() / L"drop-target";
	create_directory(drop_target_directory);

	com_ptr<IDataObject> spdo = create_data_object(local);
	com_ptr<IDropTarget> spdt = CDropTarget::Create(
		Provider(), ToRemotePath(drop_target_directory));

	POINTL pt = {0, 0};

	// Do enter with copy which sould be accepted
	DWORD dwEffect = DROPEFFECT_COPY;
	BOOST_REQUIRE_OK(spdt->DragEnter(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));

	// Continue drag
	BOOST_REQUIRE_OK(spdt->DragOver(MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));

	// Drop onto DropTarget
	BOOST_REQUIRE_OK(spdt->Drop(spdo.in(), MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_COPY));

	// Decline any further queries until next DragEnter()
	BOOST_REQUIRE_OK(spdt->DragOver(MK_LBUTTON, pt, &dwEffect));
	BOOST_REQUIRE_EQUAL(dwEffect, static_cast<DWORD>(DROPEFFECT_NONE));
}

BOOST_AUTO_TEST_SUITE_END()
#pragma endregion

BOOST_AUTO_TEST_SUITE_END()
#pragma endregion
