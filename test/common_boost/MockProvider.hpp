/**
    @file

    Mock implementation of ISftpProvider.

    @if license

    Copyright (C) 2010, 2012  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#ifndef TEST_COMMON_BOOST_MOCK_PROVIDER_HPP
#define TEST_COMMON_BOOST_MOCK_PROVIDER_HPP
#pragma once

#include "test/common_boost/tree.hpp" // tree container for mocking filesystem

#include "swish/provider/provider_error_adapter.hpp" // provider_error_adapter

#include <comet/bstr.h> // bstr_t
#include <comet/datetime.h> // datetime_t
#include <comet/smart_enum.h> // make_smart_enumeration

#include <boost/filesystem.hpp> // wpath
#include <boost/foreach.hpp> // BOOST_FOREACH
#include <boost/format.hpp> // wformat
#include <boost/make_shared.hpp> // make_shared
#include <boost/shared_ptr.hpp> // shared_ptr
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <functional> // equal_to, less
#include <string>
#include <vector>

#include <Shlwapi.h> // SHCreateMemStream

namespace test {
namespace detail {

    typedef tree<swish::SmartListing> Filesystem;
    typedef Filesystem::iterator FilesystemLocation;

    /**
     * Return an iterator to the node in the mock filesystem indicated by the
     * path given as a string.
     */
    inline FilesystemLocation find_location_from_path(
        const Filesystem& filesystem, const boost::filesystem::wpath& path)
    {
        // Start searching in root of 'filesystem'
        FilesystemLocation current_dir = filesystem.begin();

        // Walk down list of tokens finding each item below the previous
        BOOST_FOREACH(boost::filesystem::wpath segment, path.relative_path())
        {
            std::wstring name = segment.string();

            if (name == L".")
                continue;

            FilesystemLocation dir = find(
                filesystem.begin(current_dir), filesystem.end(current_dir),
                name);

            if (dir == filesystem.end(current_dir))
            {
                std::string message =
                    str(boost::format("Mock file '%s' not found") % name);
                BOOST_THROW_EXCEPTION(std::exception(message.c_str()));
            }

            current_dir = dir;
        }

        if (current_dir == filesystem.end())
            BOOST_THROW_EXCEPTION(std::exception("Unexpected lookup failure!"));

        return current_dir;
    }

    inline swish::SmartListing make_file_listing(
        comet::bstr_t name, ULONG permissions, ULONGLONG size,
        ULONG hardlink_count, comet::datetime_t date)
    {
        swish::SmartListing lt;
        lt.out()->bstrFilename = name.detach();
        lt.out()->uPermissions = permissions;
        lt.out()->bstrOwner = comet::bstr_t("mockowner").detach();
        lt.out()->bstrGroup = comet::bstr_t("mockgroup").detach();
        lt.out()->uSize = size;
        lt.out()->cHardLinks = hardlink_count;
        lt.out()->dateModified = date.get();

        return lt;
    }

    inline swish::SmartListing make_directory_listing(comet::bstr_t name)
    {
        swish::SmartListing lt = make_file_listing(
            name, 040777, 42, 7, comet::datetime_t(1601, 10, 5, 13, 54, 22));
        lt.out()->fIsDirectory = TRUE;
        return lt;
    }

    inline swish::SmartListing make_link_listing(comet::bstr_t name)
    {
        swish::SmartListing lt = make_file_listing(
            name, 040777, 42, 7, comet::datetime_t(1601, 10, 5, 13, 54, 22));
        lt.out()->fIsLink = TRUE;
        return lt;
    }

    inline comet::bstr_t tag_filename(
        const wchar_t* filename, const boost::filesystem::wpath& directory)
    {
        return str(boost::wformat(filename) % directory.filename());
    }

    inline void make_item_in(
        Filesystem& filesystem, FilesystemLocation loc,
        const swish::SmartListing& item)
    {
        filesystem.append_child(loc, item);
    }

    inline void make_item_in(
        Filesystem& filesystem, const boost::filesystem::wpath& path,
        const swish::SmartListing& item)
    {
        make_item_in(
            filesystem, find_location_from_path(filesystem, path), item);
    }

    /**
     * Generates a listing for the given directory and tags each filename with
     * the name of the parent folder.  This allows us to detect a correct
     * listing later.
     */
    inline void fill_mock_listing(
        Filesystem& filesystem, const boost::filesystem::wpath& directory)
    {
        std::vector<comet::bstr_t> filenames;
        filenames.push_back(tag_filename(L"test%sfile", directory));
        filenames.push_back(tag_filename(L"test%sFile", directory));
        filenames.push_back(tag_filename(L"test%sfile.ext", directory));
        filenames.push_back(tag_filename(L"test%sfile.txt", directory));
        filenames.push_back(tag_filename(L"test%sfile with spaces", directory));
        filenames.push_back(
            tag_filename(L"test%sfile with \"quotes\" and spaces", directory));
        filenames.push_back(tag_filename(L"test%sfile.ext.txt", directory));
        filenames.push_back(tag_filename(L"test%sfile..", directory));
        filenames.push_back(tag_filename(L".test%shiddenfile", directory));

        std::vector<comet::datetime_t> dates;
        dates.push_back(comet::datetime_t());
        dates.push_back(comet::datetime_t::now());
        dates.push_back(comet::datetime_t(1899, 7, 13, 17, 59, 12));
        dates.push_back(comet::datetime_t(9999, 12, 31, 23, 59, 59));
        dates.push_back(comet::datetime_t(2000, 2, 29, 12, 47, 1));
        dates.push_back(comet::datetime_t(1978, 3, 3, 3, 00, 00));
        dates.push_back(comet::datetime_t(1601, 1, 1, 0, 00, 00));
        dates.push_back(comet::datetime_t(2007, 2, 28, 0, 0, 0));
        dates.push_back(comet::datetime_t(1752, 9, 03, 7, 27, 8));

        unsigned long cycle = 0;
        unsigned long size = 0;
        while (!filenames.empty())
        {
            // Try to cycle through the permissions on each successive file
            // TODO: I have no idea if this works
            unsigned permissions = 
                (cycle % 1) || ((cycle % 2) << 1) || ((cycle % 3) << 2);

            make_item_in(
                filesystem, directory,
                make_file_listing(
                    filenames.back(), permissions, size, cycle, dates.back()));

            dates.pop_back();
            filenames.pop_back();
            cycle++;
            size = (size + cycle) << 10;
        }

        // Add some dummy folders also
        std::vector<comet::bstr_t> folder_names;
        folder_names.push_back(tag_filename(L"Test%sfolder", directory));
        folder_names.push_back(tag_filename(L"test%sfolder.ext", directory));
        folder_names.push_back(tag_filename(L"test%sfolder.bmp", directory));
        folder_names.push_back(
            tag_filename(L"test%sfolder with spaces", directory));
        folder_names.push_back(tag_filename(L".test%shiddenfolder", directory));

        while (!folder_names.empty())
        {
            make_item_in(
                filesystem, directory,
                make_directory_listing(folder_names.back()));
            folder_names.pop_back();
        }

        // Last but not least, links
        std::vector<comet::bstr_t> link_names;
        link_names.push_back(tag_filename(L"link%sfolder", directory));
        link_names.push_back(tag_filename(L"another link%sfolder", directory));
        link_names.push_back(tag_filename(L"p%s", directory));
        link_names.push_back(tag_filename(L".q%s", directory));
        link_names.push_back(tag_filename(L"this_link_is_broken_%s", directory));

        while (!link_names.empty())
        {
            make_item_in(
                filesystem, directory, make_link_listing(link_names.back()));
            link_names.pop_back();
        }
    }
}

class MockProvider :
    public comet::simple_object<swish::provider::provider_error_adapter>
{
public:

    /**
    * Possible behaviours of listing returned by mock GetListing() method.
    */
    typedef enum tagListingBehaviour {
        MockListing,     ///< Return a dummy list of files and S_OK.
        EmptyListing,    ///< Return an empty list and S_OK.
        SFalseNoListing, ///< Return a NULL listing and S_FALSE.
        AbortListing,    ///< Return a NULL listing E_ABORT.
        FailListing      ///< Return a NULL listing E_FAIL.
    } ListingBehaviour;

    /**
    * Possible behaviours of mock Rename() method.
    */
    typedef enum tagRenameBehaviour {
        RenameOK,           ///< Return S_OK - rename unconditionally succeeded.
        ConfirmOverwrite,   ///< Call ISftpConsumer's OnConfirmOverwrite and
        ///< return its return value.
        AbortRename,        ///< Return E_ABORT.
        FailRename          ///< Return E_FAIL.
    } RenameBehaviour;

    MockProvider() :
        m_listing_behaviour(MockListing), m_rename_behaviour(RenameOK)
    {
        // Create filesystem root
        detail::FilesystemLocation root = m_filesystem.insert(
            m_filesystem.begin(), detail::make_directory_listing(L"/"));

        // Create two subdirectories and fill them with an expected set of items
        // whose names are 'tagged' with the directory name
        detail::FilesystemLocation tmp =
            m_filesystem.append_child(
                root, detail::make_directory_listing(L"tmp"));
        detail::FilesystemLocation swish =
            m_filesystem.append_child(
                tmp, detail::make_directory_listing(L"swish"));
        detail::fill_mock_listing(m_filesystem, L"/tmp");
        detail::fill_mock_listing(m_filesystem, L"/tmp/swish");
    }

    ~MockProvider()
    {
        m_filesystem.clear();
    }

    void set_listing_behaviour(ListingBehaviour behaviour)
    {
        m_listing_behaviour = behaviour;
    }

    void set_rename_behaviour(RenameBehaviour behaviour)
    {
        m_rename_behaviour = behaviour;
    }

    virtual void initialize(BSTR /*user*/, BSTR /*host*/, UINT /*port*/) {}

    virtual IEnumListing* get_listing(
        ISftpConsumer* /*consumer*/, BSTR directory)
    {
        boost::shared_ptr< std::vector<swish::SmartListing> > files = 
            boost::make_shared< std::vector<swish::SmartListing> >();

        switch (m_listing_behaviour)
        {
        case EmptyListing:
            break;

        case MockListing:
            {
                detail::FilesystemLocation dir =
                    detail::find_location_from_path(m_filesystem, directory);

                // Copy directory out of tree and sort alphabetically
                files->insert(
                    files->begin(), m_filesystem.begin(dir),
                    m_filesystem.end(dir));
                std::sort(files->begin(), files->end());
            }
            break;

        case SFalseNoListing:
            BOOST_THROW_EXCEPTION(comet::com_error(S_FALSE));

        case AbortListing:
            BOOST_THROW_EXCEPTION(comet::com_error(E_ABORT));

        case FailListing:
            BOOST_THROW_EXCEPTION(comet::com_error(E_FAIL));

        default:
            BOOST_THROW_EXCEPTION(comet::com_error(
                "Unreachable: Unrecognised mock behaviour", E_UNEXPECTED));
        }

        return comet::make_smart_enumeration<IEnumListing>(files).detach();
    }

    virtual IStream* get_file(
        ISftpConsumer* /*consumer*/, BSTR file_path, BOOL /*writeable*/)
    {
        detail::find_location_from_path(
            m_filesystem, file_path); // test existence

        // Create IStream instance whose data is the file path
        return ::SHCreateMemStream(
            reinterpret_cast<BYTE*>(file_path), ::SysStringByteLen(file_path));
    }

    virtual VARIANT_BOOL rename(
        ISftpConsumer* consumer, BSTR from_path, BSTR to_path)
    {
        detail::find_location_from_path(
            m_filesystem, from_path); // test existence

        switch (m_rename_behaviour)
        {
        case RenameOK:
            return VARIANT_FALSE;

        case ConfirmOverwrite:
            {
                HRESULT hr = consumer->OnConfirmOverwrite(from_path, to_path);
                if (SUCCEEDED(hr))
                    return VARIANT_TRUE;
                BOOST_THROW_EXCEPTION(
                    comet::com_error_from_interface<ISftpConsumer>(
                        consumer, hr));
            }

        case AbortRename:
            BOOST_THROW_EXCEPTION(comet::com_error(E_ABORT));

        case FailRename:
            BOOST_THROW_EXCEPTION(comet::com_error(E_FAIL));

        default:
            BOOST_THROW_EXCEPTION(comet::com_error(
                "Unreachable: Unrecognised mock behaviour", E_UNEXPECTED));
        }
    }

    virtual void delete_file(ISftpConsumer* /*consumer*/, BSTR /*path*/)
    {};

    virtual void delete_directory(ISftpConsumer* /*consumer*/, BSTR /*path*/)
    {};

    virtual void create_new_file(ISftpConsumer* /*consumer*/, BSTR /*path*/)
    {};

    virtual void create_new_directory(
        ISftpConsumer* /*consumer*/, BSTR /*path*/)
    {};

    virtual BSTR resolve_link(ISftpConsumer* /*consumer*/, BSTR path)
    {
        std::wstring p(path);

        // link names with 'broken' in their name we pretend to resolve to
        // a target that doesn't exist
        if (p.find(L"broken") != std::wstring::npos)
            return comet::bstr_t(L"/tmp/broken_link_target").detach();

        // link names with 'folder' in their name we pretend target a directory
        // (/tmp/testtmpfolder) and the others we target at a file
        // (/tmp/testfile)
        else if (p.find(L"folder") != std::wstring::npos)
            return comet::bstr_t(L"/tmp/Testtmpfolder").detach();
        else
            return comet::bstr_t(L"/tmp/testtmpfile").detach();
    };

    virtual Listing stat(ISftpConsumer* consumer, BSTR path, BOOL fFollowLinks)
    {
        boost::filesystem::wpath target;
        if (fFollowLinks)
        {
            target =
                comet::bstr_t(
                    comet::auto_attach(resolve_link(consumer, path))).w_str();
        }
        else
        {
            target = comet::bstr_t(path).w_str();
        }

        detail::FilesystemLocation dir =
            detail::find_location_from_path(m_filesystem, target);

        // copy listing because caller gets ownership
        swish::SmartListing return_listing = *dir;
        return return_listing.detach();
    }

private:

    provider_interface& impl() { return *this; }

    detail::Filesystem m_filesystem;
    ListingBehaviour m_listing_behaviour;
    RenameBehaviour m_rename_behaviour;
};

} // namespace test

#endif
