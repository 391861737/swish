/**
    @file

    Plan copying items in PIDL clipboard format to remote server.

    @if license

    Copyright (C) 2012  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#ifndef SWISH_DROP_TARGET_PIDLCOPYPLAN_HPP
#define SWISH_DROP_TARGET_PIDLCOPYPLAN_HPP
#pragma once

#include "swish/drop_target/Plan.hpp"
#include "swish/drop_target/SequentialPlan.hpp"
#include "swish/shell_folder/data_object/ShellDataObject.hpp"  // PidlFormat

namespace swish {
namespace drop_target {

/**
 * Plan copying items in PIDL clipboard format to remote server.
 */
class PidlCopyPlan : public Plan
{
public:

	PidlCopyPlan(const swish::shell_folder::data_object::PidlFormat& format);

public: // Plan

	virtual const Operation& operator[](unsigned int i) const;

	virtual std::size_t size() const;

	virtual void execute_plan(
		const winapi::shell::pidl::apidl_t& remote_destination_root,
		Progress& progress, comet::com_ptr<ISftpProvider> provider,
		comet::com_ptr<ISftpConsumer> consumer, CopyCallback& callback)
		const;

public:

	void add_stage(const Operation& entry);

private:

	SequentialPlan m_plan;
};

}}

#endif