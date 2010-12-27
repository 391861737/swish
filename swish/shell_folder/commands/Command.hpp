/**
    @file

    Swish host folder commands.

    @if license

    Copyright (C) 2010  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include <winapi/shell/pidl.hpp> // apidl_t

#include <comet/ptr.h> // com_ptr
#include <comet/uuid_fwd.h> // uuid_t

#include <boost/function.hpp> // function
#include <boost/preprocessor.hpp> // creating variadic pass-through contructors

#include <ObjIdl.h> // IDataObject, IBindCtx
#include <shobjidl.h> // IExplorerCommandProvider, IShellItemArray

#include <string>

namespace swish {
namespace shell_folder {
namespace commands {

class Command
{
public:
	Command(
		const std::wstring& title, const comet::uuid_t& guid,
		const std::wstring& tool_tip=std::wstring(),
		const std::wstring& icon_descriptor=std::wstring(),
		const std::wstring& menu_title=std::wstring(),
		const std::wstring& webtask_title=std::wstring());

	virtual ~Command() {}

	/**
	 * Invoke to perform the command.
	 *
	 * Concrete commands will provide their implementation by overriding
	 * this method.
	 *
	 * @param data_object  DataObject holding items on which to perform the
	 *                     command.  This may be NULL in which case the
	 *                     command should only execute if it makes sense to
	 *                     do so regardless of selected items.
	 */
	virtual void operator()(
		const comet::com_ptr<IDataObject>& data_object,
		const comet::com_ptr<IBindCtx>& bind_ctx) const = 0;

	/** @name Attributes. */
	// @{
	const comet::uuid_t& guid() const;
	std::wstring title(
		const comet::com_ptr<IDataObject>& data_object) const;
	std::wstring tool_tip(
		const comet::com_ptr<IDataObject>& data_object) const;
	std::wstring icon_descriptor(
		const comet::com_ptr<IDataObject>& data_object) const;

	/** @name Optional title variants. */
	// @{
	std::wstring menu_title(
		const comet::com_ptr<IDataObject>& data_object) const;
	std::wstring webtask_title(
		const comet::com_ptr<IDataObject>& data_object) const;
	// @}

	// @}

	/** @name State. */
	// @{
	virtual bool disabled(
		const comet::com_ptr<IDataObject>& data_object,
		bool ok_to_be_slow) const = 0;
	virtual bool hidden(
		const comet::com_ptr<IDataObject>& data_object,
		bool ok_to_be_slow) const = 0;
	// @}

private:
	std::wstring m_title;
	comet::uuid_t m_guid;
	std::wstring m_tool_tip;
	std::wstring m_icon_descriptor;
	std::wstring m_menu_title;
	std::wstring m_webtask_title;
};


#ifndef COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS
#define COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS 10
#endif

#define COMMAND_ADAPTER_VARIADIC_CONSTRUCTOR(N, classname, initialiser) \
	BOOST_PP_EXPR_IF(N, template<BOOST_PP_ENUM_PARAMS(N, typename A)>) \
	explicit classname(BOOST_PP_ENUM_BINARY_PARAMS(N, A, a)) \
		: initialiser(BOOST_PP_ENUM_PARAMS(N, a)) {}

template<typename CommandImpl>
class CommandTitleAdapter
{
public:

// Define pass-through contructors with variable numbers of arguments
#define BOOST_PP_LOCAL_MACRO(N) \
	COMMAND_ADAPTER_VARIADIC_CONSTRUCTOR(N, CommandTitleAdapter, m_command)

#define BOOST_PP_LOCAL_LIMITS (0, COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS)
#include BOOST_PP_LOCAL_ITERATE()

	void operator()(
		const comet::com_ptr<IDataObject>& data_object,
		const comet::com_ptr<IBindCtx>& bind_ctx) const
	{ return m_command(data_object, bind_ctx); }

	const comet::uuid_t& guid() const { return m_command.guid(); }

	std::wstring tool_tip(
		const comet::com_ptr<IDataObject>& data_object) const
	{ return m_command.tool_tip(data_object); }

	std::wstring icon_descriptor(
		const comet::com_ptr<IDataObject>& data_object) const
	{ return m_command.icon_descriptor(data_object); }

	bool disabled(
		const comet::com_ptr<IDataObject>& data_object,
		bool ok_to_be_slow) const
	{ return m_command.disabled(data_object, ok_to_be_slow); }

	bool hidden(
		const comet::com_ptr<IDataObject>& data_object,
		bool ok_to_be_slow) const
	{ return m_command.hidden(data_object, ok_to_be_slow); }

protected:
	CommandImpl& command() { return m_command; }
	const CommandImpl& command() const { return m_command; }

private:
	CommandImpl m_command;
};

template<typename CommandImpl>
class MenuCommandTitleAdapter : public CommandTitleAdapter<CommandImpl>
{
public:

// Define pass-through contructors with variable numbers of arguments
#define BOOST_PP_LOCAL_MACRO(N) \
	COMMAND_ADAPTER_VARIADIC_CONSTRUCTOR( \
		N, MenuCommandTitleAdapter, CommandTitleAdapter<CommandImpl>)

#define BOOST_PP_LOCAL_LIMITS (0, COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS)
#include BOOST_PP_LOCAL_ITERATE()

	std::wstring title(const comet::com_ptr<IDataObject>& data_object) const
	{ return command().menu_title(data_object); }
};


template<typename CommandImpl>
class WebtaskCommandTitleAdapter : public CommandTitleAdapter<CommandImpl>
{
public:

// Define pass-through contructors with variable numbers of arguments
#define BOOST_PP_LOCAL_MACRO(N) \
	COMMAND_ADAPTER_VARIADIC_CONSTRUCTOR( \
		N, WebtaskCommandTitleAdapter, CommandTitleAdapter<CommandImpl>)

#define BOOST_PP_LOCAL_LIMITS (0, COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS)
#include BOOST_PP_LOCAL_ITERATE()

	std::wstring title(const comet::com_ptr<IDataObject>& data_object) const
	{ return command().webtask_title(data_object); }
};

#undef COMMAND_ADAPTER_CONSTRUCTOR_MAX_ARGUMENTS
#undef COMMAND_ADAPTER_VARIADIC_CONSTRUCTOR

}}} // namespace swish::shell_folder::commands
