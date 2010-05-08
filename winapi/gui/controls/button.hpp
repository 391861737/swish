/**
    @file

    GUI button control.

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

#ifndef WINAPI_GUI_CONTROLS_BUTTON_HPP
#define WINAPI_GUI_CONTROLS_BUTTON_HPP
#pragma once

#include <winapi/gui/controls/control.hpp> // control base class
#include <winapi/gui/commands.hpp> // command<BN_CLICKED>
#include <winapi/gui/detail/window_impl.hpp> // window_impl

#include <boost/shared_ptr.hpp> // shared_ptr
#include <boost/signal.hpp> // signal

#include <string>

namespace winapi {
namespace gui {
namespace controls {

class button_impl : public winapi::gui::detail::window_impl
{
public:

	button_impl(
		const std::wstring& title, short left, short top, short width,
		short height, bool default)
		:
		winapi::gui::detail::window_impl(title, left, top, width, height),
		m_default(default) {}

	std::wstring window_class() const { return L"button"; }

	DWORD style() const
	{
		DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
		
		style |= (m_default) ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;

		return style;
	}

	boost::signal<void ()>& on_click() { return m_on_click; }

private:
	void on(command<BN_CLICKED>) { m_on_click(); }

	boost::signal<void ()> m_on_click;
	bool m_default;
};

class button : public control<button_impl>
{
public:
	button(
		const std::wstring& title, short left, short top, short width,
		short height, bool default=false)
		:
		control<button_impl>(
			boost::shared_ptr<button_impl>(
				new button_impl(title, left, top, width, height, default))) {}

	boost::signal<void ()>& on_click() { return impl()->on_click(); }

	short left() const { return impl()->left(); }
	short top() const { return impl()->top(); }
	short width() const { return impl()->width(); }
	short height() const { return impl()->height(); }
};

}}} // namespace winapi::gui::controls

#endif
