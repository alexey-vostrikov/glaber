<?php
/*
** Glaber
** Copyright (C) 2001-2021 Glaber JSC
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


class CControllerDebugList extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {

		$fields = [
			'triggerid' =>			'string',
			'itemid'	=>	'string',
			'apply_new' => 'string'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return $this->checkAccess(CRoleHelper::UI_ADMINISTRATION_MEDIA_TYPES);
	}

	protected function doAction() {
		$itemid = $this->getInput('itemid', 0 );
		$triggerid = $this->getInput('triggerid', 0);
		$apply_new = $this->getInput('apply_new', 0);
		
		if ( $apply_new > 0 ) {
			CZabbixServer::setDebugObjects($itemid, $triggerid);
		} else {
			$response = CZabbixServer::getDebugObjects();
						
			if (isset($response['itemid'])) 
				$itemid = $response['itemid'];
			
			if (isset($response['triggerid'])) 
				$triggerid = $response['triggerid'];

		}

		$data = [
			'triggerid' => $triggerid,
			'itemid' => $itemid
		];
		
		$response = new CControllerResponseData($data);
		$response->setTitle(_('Debug objects'));
		$this->setResponse($response);
	}
}
