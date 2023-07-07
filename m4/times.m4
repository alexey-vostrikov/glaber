# Zabbix
# Copyright (C) 2001-2023 Zabbix SIA
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# TIMES_CHECK_NULL_ARG
#
#   Checks if times function can be called with NULL argument and sets TIMES_SUPPORTS_NULL_ARG if true
#

AC_DEFUN([TIMES_TRY_RUN],
[
	AC_RUN_IFELSE(
	[
		AC_LANG_SOURCE(
		#include <sys/times.h>
		#include <stdlib.h>

		int	main()
		{
			
			if((clock_t)-1 == times(NULL))
				return 1;

			return 0;
		}
		)
	]
	,
	test_times_null_arg="yes",
	test_times_null_arg="no",
	test_times_null_arg="no" dnl action-if-cross-compiling
	)
])

AC_DEFUN([TIMES_CHECK_NULL_ARG],
[
	AC_MSG_CHECKING(if 'times' function supports null argument)
	
	TIMES_TRY_RUN([no])

	if test "x$test_times_null_arg" = "xyes"; then
		AC_DEFINE([TIMES_NULL_ARG], 1, [Define to 1 if 'times' function supports NULL argument])
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
	fi
])dnl
