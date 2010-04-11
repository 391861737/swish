/**
    @file

    Form class for login password prompt.

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

#include "password.hpp"

#include <winapi/gui/controls/button.hpp> // button
#include <winapi/gui/controls/edit.hpp> // edit
#include <winapi/gui/controls/label.hpp> // label
#include <winapi/gui/form.hpp> // form
#include <winapi/gui/hooks.hpp> // creation_hooks

#include <boost/bind.hpp> // bind
#include <boost/locale.hpp> // translate

#include <exception> // exception
#include <string>

using winapi::gui::form;
using namespace winapi::gui::controls;

using boost::bind;
using boost::locale::translate;

using std::wstring;

namespace swish {
namespace shell_folder {
namespace forms {

namespace {

	class PasswordForm
	{
	public:
		PasswordForm(HWND hwnd_owner, const wstring& prompt)
			:
			m_form(translate("Password"), 219, 49, 0, 0),
			m_cancelled(false), m_password_box(edit(L"", 148, 14, 7, 18, true))
		{
			m_form.add_control(m_password_box);
			m_form.add_control(label(prompt, 149, 8, 7, 7));

			m_form.add_control(
				button(
					translate("OK"), 50, 16, 162, 7,
					bind(&form::end, boost::ref(m_form)), true));

			m_form.add_control(
				button(
					translate("Cancel"), 50, 16, 162, 26, 
					bind(&PasswordForm::on_cancel, boost::ref(*this))));
			

			m_form.show(hwnd_owner);
		}

		void on_cancel()
		{
			m_form.end();
			m_cancelled = true;
		}

		bool was_cancelled() const
		{ return m_cancelled; }

		wstring password() const
		{ return m_password_box.text(); }

	private:
		winapi::gui::creation_hooks<wchar_t> m_hooks;
		form m_form;
		bool m_cancelled;
		edit m_password_box;
	};
}

wstring password_prompt(HWND hwnd_owner, const wstring& prompt)
{
	PasswordForm pass_form(hwnd_owner, prompt);
	if (pass_form.was_cancelled())
		throw std::exception("user cancelled without entering password");
	else
		return pass_form.password();
}
	
}}} // namespace swish::shell_folder::forms
