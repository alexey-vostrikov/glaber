
/*
** Copyright Glaber
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/
/* note: https://www.atlassian.com/incident-management/devops/incident-vs-problem-management 
  in zabbix this part of business is called "problems", but in Glaber - it's "incidents"
*/

/*note: move problems state code from the state_problems.c to incidents */

int state_incidents_init(mem_funcs_t *memf);


int incidents_process_trigger_fired();

/* note - it's still quiestinable if OK is proper naming for the non-fired state */
int incidents_process_trigger_ok();

/* cleanup outdated incidents */
int incidents_housekeep();