/**
    @file

    Explorer tool-bar command button implementation classes.

    @if licence

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

#include "explorer_command.hpp"

#include "swish/catch_com.hpp" // catchCom
#include "swish/exception.hpp" // com_exception

#include <comet/uuid_fwd.h> // uuid_t
#include <comet/enum.h> // stl_enumeration
#include <comet/ptr.h> // com_ptr

#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION
#include <boost/foreach.hpp> // BOOST_FOREACH

using swish::exception::com_exception;
using swish::shell_folder::commands::Command;

using comet::uuid_t;
using comet::stl_enumeration;
using comet::com_ptr;

using std::wstring;

template<> struct comet::enumerated_type_of<IEnumExplorerCommand>
{ typedef IExplorerCommand* is; };

template<> struct comet::impl::type_policy<IExplorerCommand*>
{
	template<typename S>
	static void init(IExplorerCommand*& p, const S& s) 
	{  p = s.get(); p->AddRef(); }

	static void clear(IExplorerCommand*& p) { p->Release(); }	
};

namespace swish {
namespace shell_folder {
namespace explorer_command {

#pragma region CExplorerCommandProvider implementation

/**
 * Create an ExplorerCommandProvider from exisiting ExplorerCommands.
 *
 * Store the ordered vector of commands and build a mapping from GUIDs
 * to IExplorerCommands for use when looking up via GetCommand.
 */
CExplorerCommandProvider::CExplorerCommandProvider(
	const ordered_commands& commands) : m_commands(commands)
{
	BOOST_FOREACH(comet::com_ptr<IExplorerCommand>& c, m_commands)
	{
		uuid_t guid;
		HRESULT hr = c->GetCanonicalName(guid.out());
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));
		m_guid_mapping[guid] = c;
	}
}

STDMETHODIMP CExplorerCommandProvider::GetCommands(
	IUnknown* /*punkSite*/, const IID& riid, void** ppv)
{
	if (ppv)
		*ppv = NULL;
	else
		return E_POINTER;

	try
	{
		com_ptr<IEnumExplorerCommand> commands =
			stl_enumeration<IEnumExplorerCommand>::create(
				m_commands, get_unknown());
		return commands->QueryInterface(riid, ppv);
	}
	catchCom();

	return S_OK;
}

STDMETHODIMP CExplorerCommandProvider::GetCommand(
	const GUID& rguidCommandId, const IID& riid, void** ppv)
{
	if (ppv)
		*ppv = NULL;
	else
		return E_POINTER;

	try
	{
		command_map::const_iterator item = m_guid_mapping.find(rguidCommandId);
		if (item == m_guid_mapping.end())
			BOOST_THROW_EXCEPTION(com_exception(E_FAIL));

		return item->second->QueryInterface(riid, ppv);
	}
	catchCom();

	return S_OK;
}

#pragma endregion

#pragma region CExplorerCommandImpl implementation

/**
 * Return command's title string.
 *
 * @param[in]  psiItemArray  Optional array of PIDLs that command would be
 *                           executed upon.
 * @param[out] ppszName      Location in which to return character buffer
 *                           allocated with CoTakMemAlloc.
 */
STDMETHODIMP CExplorerCommandImpl::GetTitle(
	IShellItemArray* psiItemArray, wchar_t** ppszName)
{
	if (ppszName)
		*ppszName = NULL;
	else
		return E_POINTER;

	try
	{
		HRESULT hr = ::SHStrDup(title(psiItemArray).c_str(), ppszName);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));
	}
	catchCom();

	return S_OK;
}

/**
 * Return command's icon descriptor.
 *
 * This takes the form "shell32.dll,-249" where 249 is the icon's resource ID.
 *
 * @param[in]  psiItemArray  Optional array of PIDLs that command would be
 *                           executed upon.
 * @param[out] ppszIcon      Location in which to return character buffer
 *                           allocated with CoTakMemAlloc.
 */
STDMETHODIMP CExplorerCommandImpl::GetIcon(
	IShellItemArray* psiItemArray, wchar_t** ppszIcon)
{
	if (ppszIcon)
		*ppszIcon = NULL;
	else
		return E_POINTER;

	try
	{
		HRESULT hr = ::SHStrDup(icon(psiItemArray).c_str(), ppszIcon);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));
	}
	catchCom();

	return S_OK;
}

/**
 * Return command's tool tip.
 *
 * @param[in]  psiItemArray  Optional array of PIDLs that command would be
 *                           executed upon.
 * @param[out] ppszInfotip   Location in which to return character buffer
 *                           allocated with CoTakMemAlloc.
 */
STDMETHODIMP CExplorerCommandImpl::GetToolTip(
	IShellItemArray* psiItemArray, wchar_t** ppszInfotip)
{
	if (ppszInfotip)
		*ppszInfotip = NULL;
	else
		return E_POINTER;

	try
	{
		HRESULT hr = ::SHStrDup(tool_tip(psiItemArray).c_str(), ppszInfotip);
		if (FAILED(hr))
			BOOST_THROW_EXCEPTION(com_exception(hr));
	}
	catchCom();

	return S_OK;
}

/**
 * Return command's unique GUID.
 *
 * @param[out] pguidCommandName   Location in which to return GUID.
 */
STDMETHODIMP CExplorerCommandImpl::GetCanonicalName(GUID* pguidCommandName)
{
	if (pguidCommandName)
		*pguidCommandName = GUID_NULL;
	else
		return E_POINTER;

	try
	{
		*pguidCommandName = canonical_name();
	}
	catchCom();

	return S_OK;
}

/**
 * Return the command's state given array of PIDLs.
 *
 * @param[in]  psiItemArray  Optional array of PIDLs that command would be
 *                           executed upon.
 * @param[in]  fOkToBeSlow   Indicated whether slow operations can be used
 *                           when calculating the state.
 * @param[out] pCmdState     Location in which to return the state flags.
 */
STDMETHODIMP CExplorerCommandImpl::GetState(
	IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState)
{
	if (pCmdState)
		*pCmdState = 0;
	else
		return E_POINTER;

	try
	{
		*pCmdState = state(psiItemArray, (fOkToBeSlow) ? true : false);
	}
	catchCom();

	return S_OK;
}

/**
 * Execute the code associated with this command instance.
 *
 * @param[in] psiItemArray  Optional array of PIDLs that command is
 *                          executed upon.
 * @param[in] pbc           Optional bind context.
 */
STDMETHODIMP CExplorerCommandImpl::Invoke(
	IShellItemArray* psiItemArray, IBindCtx* pbc)
{
	try
	{
		invoke(psiItemArray, pbc);
	}
	catchCom();

	return S_OK;
}

STDMETHODIMP CExplorerCommandImpl::GetFlags(EXPCMDFLAGS* pFlags)
{
	if (pFlags)
		*pFlags = 0;
	else
		return E_POINTER;

	try
	{
		*pFlags = flags();
	}
	catchCom();

	return S_OK;
}

STDMETHODIMP CExplorerCommandImpl::EnumSubCommands(
	IEnumExplorerCommand** ppEnum)
{
	if (ppEnum)
		*ppEnum = NULL;
	else
		return E_POINTER;

	try
	{
		*ppEnum = subcommands().detach();
	}
	catchCom();

	return S_OK;
}

#pragma endregion

}}} // namespace swish::shell_folder::explorer_command
