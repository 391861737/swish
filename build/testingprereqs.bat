:: Script to fetch Swish unit testing build prerequisites
:: 
:: Copyright (C) 2009  Alexander Lamaison <awl03@doc.ic.ac.uk>
:: 
:: This program is free software; you can redistribute it and/or modify
:: it under the terms of the GNU General Public License as published by
:: the Free Software Foundation; either version 2 of the License, or
:: (at your option) any later version.
:: 
:: This program is distributed in the hope that it will be useful,
:: but WITHOUT ANY WARRANTY; without even the implied warranty of
:: MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
:: GNU General Public License for more details.
:: 
:: You should have received a copy of the GNU General Public License along
:: with this program; if not, write to the Free Software Foundation, Inc.,
:: 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

@echo off
setlocal
echo.

cd ..\thirdparty
set WGET=..\build\wget\wget.exe -N
set SEVENZ=..\build\7za\7za.exe

:: Boost.Process

echo ===- Dowloading Boost.Process ...
%WGET% "http://www.highscore.de/boost/process.zip" || (
	echo ===- Error while trying to download Boost.Process. & goto error)
%SEVENZ% x process.zip -oboost.process -aoa || (
	echo ===- Error while trying to extract Boost.Process. & goto error)
del process.zip

:: Comet

echo ===- Dowloading Comet ...
%WGET% "http://downloads.sourceforge.net/project/swish/build-prereqs/comet-1.31beta-vs2005.7z" || (
	echo ===- Error while trying to download Comet. & goto error)
%SEVENZ% x comet-1.31beta-vs2005.7z -ocomet -aoa || (
	echo ===- Error while trying to extract Comet. & goto error)
del comet-1.31beta-vs2005.7z

echo.
echo ===- All testing prerequisites successfully created.
echo.
pause
exit /B

:error
echo.
pause
exit /B 1