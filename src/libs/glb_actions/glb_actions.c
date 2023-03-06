/*
** Copyright Glaber 2018-2023
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

/* actions are action-object specific, actions are created during some processing events
and so they shouldn't use DB as might be used on the data plane */
#include "zbxcommon.h"
#include "log.h"

int glb_actions_process_discovery_host() {
  //  HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_lld_status() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_autoregister() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_problem_new() {
    //HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_problem_recovery() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_by_acknowledgments() {
    //HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_discovery_service() {
//     HALT_HERE("Not implemented yet: %s", __func__ );
}
