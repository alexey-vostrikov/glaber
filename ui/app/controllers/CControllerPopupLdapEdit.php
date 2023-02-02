<?php declare(strict_types = 0);
/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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


class CControllerPopupLdapEdit extends CController {

	const LDAP_MEMBER_OF = 0;
	const LDAP_GROUP_OF_NAMES = 1;

	protected function checkInput(): bool {
		$fields = [
			'row_index' =>						'required|int32',
			'userdirectoryid' =>				'db userdirectory.userdirectoryid',
			'name' =>							'db userdirectory.name',
			'host' =>							'db userdirectory_ldap.host',
			'port' =>							'db userdirectory_ldap.port|ge '.ZBX_MIN_PORT_NUMBER.'|le '.ZBX_MAX_PORT_NUMBER,
			'base_dn' =>						'db userdirectory_ldap.base_dn',
			'bind_dn' =>						'db userdirectory_ldap.bind_dn',
			'bind_password' =>					'db userdirectory_ldap.bind_password',
			'search_attribute' =>				'db userdirectory_ldap.search_attribute',
			'start_tls' =>						'in '.ZBX_AUTH_START_TLS_OFF.','.ZBX_AUTH_START_TLS_ON,
			'search_filter' =>					'db userdirectory_ldap.search_filter',
			'case_sensitive' =>					'in '.ZBX_AUTH_CASE_INSENSITIVE.','.ZBX_AUTH_CASE_SENSITIVE,
			'description' =>					'db userdirectory.description',
			'group_basedn' =>					'db userdirectory_ldap.group_basedn',
			'group_name' =>						'db userdirectory_ldap.group_name',
			'group_member' =>					'db userdirectory_ldap.group_member',
			'user_ref_attr' =>					'db userdirectory_ldap.user_ref_attr',
			'group_filter' =>					'db userdirectory_ldap.group_filter',
			'group_membership' =>				'db userdirectory_ldap.group_membership',
			'user_username' =>					'db userdirectory_ldap.user_username',
			'user_lastname' =>					'db userdirectory_ldap.user_lastname',
			'add_ldap_server' =>				'in 0,1',
			'group_configuration' =>			'in '.self::LDAP_MEMBER_OF.','.self::LDAP_GROUP_OF_NAMES,
			'provision_status' =>				'in '.JIT_PROVISIONING_DISABLED.','.JIT_PROVISIONING_ENABLED,
			'provision_groups' =>				'array',
			'provision_media' =>				'array'
		];

		$ret = $this->validateInput($fields) && $this->validateProvisionGroups() && $this->validateProvisionMedia();

		if (!$ret) {
			$this->setResponse(
				(new CControllerResponseData([
					'main_block' => json_encode([
						'error' => [
							'title' => _('Invalid LDAP configuration'),
							'messages' => array_column(get_and_clear_messages(), 'message')
						]
					])
				]))->disableView()
			);
		}

		return $ret;
	}

	/**
	 * @throws APIException
	 */
	protected function checkPermissions(): bool {
		return $this->checkAccess(CRoleHelper::UI_ADMINISTRATION_AUTHENTICATION);
	}

	/**
	 * @throws Exception
	 */
	protected function doAction(): void {
		$data = [
			'row_index' => $this->getInput('row_index'),
			'name' => $this->getInput('name', ''),
			'host' => $this->getInput('host', ''),
			'port' => $this->getInput('port', '389'),
			'base_dn' => $this->getInput('base_dn', ''),
			'search_attribute' => $this->getInput('search_attribute', ''),
			'start_tls' => $this->getInput('start_tls', ZBX_AUTH_START_TLS_OFF),
			'bind_dn' => $this->getInput('bind_dn', ''),
			'description' => $this->getInput('description', ''),
			'search_filter' => $this->getInput('search_filter', ''),
			'group_basedn' => $this->getInput('group_basedn', ''),
			'group_name' => $this->getInput('group_name', ''),
			'group_member' => $this->getInput('group_member', ''),
			'user_ref_attr' => $this->getInput('user_ref_attr', ''),
			'group_filter' => $this->getInput('group_filter', ''),
			'group_membership' => $this->getInput('group_membership', ''),
			'user_username' => $this->getInput('user_username', ''),
			'user_lastname' => $this->getInput('user_lastname', ''),
			'provision_status' => $this->getInput('provision_status', JIT_PROVISIONING_DISABLED),
			'add_ldap_server' => $this->getInput('add_ldap_server', 1),
			'userdirectoryid' => $this->hasInput('userdirectoryid') ? $this->getInput('userdirectoryid') : null,
			'provision_groups' => $this->getInput('provision_groups', []),
			'provision_media' => $this->getInput('provision_media', []),
			'group_configuration' => $this->getInput('group_configuration', self::LDAP_MEMBER_OF),
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		];

		if ($this->hasInput('bind_password')) {
			$data['bind_password'] = $this->getInput('bind_password');
		}

		$data['advanced_configuration'] = ($data['start_tls'] != ZBX_AUTH_START_TLS_OFF
			|| $data['search_filter'] !== ''
		);

		if (!$this->hasInput('group_configuration')) {
			$group_filter = $data['group_basedn'].$data['group_member'].$data['user_ref_attr'].$data['group_filter'];
			$data['group_configuration'] = $group_filter === '' ? self::LDAP_MEMBER_OF : self::LDAP_GROUP_OF_NAMES;
		}

		self::extendProvisionGroups($data['provision_groups']);
		self::extendProvisionMedia($data['provision_media']);

		$this->setResponse(new CControllerResponseData($data));
	}

	private static function extendProvisionGroups(array &$provision_groups): void {
		$roleids = [];
		$usrgrpids = [];

		foreach ($provision_groups as $group) {
			$roleids[$group['roleid']] = $group['roleid'];
			foreach ($group['user_groups'] as $user_group) {
				$usrgrpids[$user_group] = $user_group;
			}
		}

		$roles = $roleids
			? API::Role()->get([
				'output' => ['name'],
				'roleids' => $roleids,
				'preservekeys' => true
			])
			: [];

		$user_groups = $usrgrpids
			? API::UserGroup()->get([
				'output' => ['name'],
				'usrgrpids' => $usrgrpids,
				'preservekeys' => true
			])
			: [];

		foreach ($provision_groups as &$provision_group) {
			$provision_group['role_name'] = $roles[$provision_group['roleid']]['name'];

			foreach ($provision_group['user_groups'] as &$user_group) {
				$user_group = [
					'name' => $user_groups[$user_group]['name'],
					'usrgrpid' => $user_group
				];
			}
			unset($user_group);
		}
		unset($provision_group);
	}

	private static function extendProvisionMedia(array &$provision_media): void {
		$mediatypes = API::MediaType()->get([
			'output' => ['name'],
			'mediatypeids' => array_column($provision_media, 'mediatypeid'),
			'preservekeys' => true
		]);

		foreach ($provision_media as $index => $media) {
			if (!array_key_exists($media['mediatypeid'], $mediatypes)) {
				unset($provision_media[$index]);
				continue;
			}

			$provision_media[$index]['mediatype_name'] = $mediatypes[$media['mediatypeid']]['name'];
		}
	}

	private function validateProvisionGroups(): bool {
		if ($this->getInput('provision_status', JIT_PROVISIONING_DISABLED) != JIT_PROVISIONING_ENABLED) {
			return true;
		}

		foreach ($this->getInput('provision_groups', []) as $group) {
			if (!is_array($group)
					|| !array_key_exists('user_groups', $group) || !is_array($group['user_groups'])
					|| !array_key_exists('roleid', $group) || !ctype_digit($group['roleid'])) {
				return false;
			}
		}

		return true;
	}

	private function validateProvisionMedia(): bool {
		if (!$this->hasInput('provision_media')) {
			return true;
		}

		foreach ($this->getInput('provision_media') as $media) {
			if (!is_array($media)
					|| !array_key_exists('name', $media) || !is_string($media['name']) || $media['name'] === ''
					|| !array_key_exists('attribute', $media) || !is_string($media['attribute'])
						|| $media['attribute'] === ''
					|| !array_key_exists('mediatypeid', $media) || !ctype_digit($media['mediatypeid'])) {
				return false;
			}
		}

		return true;
	}
}
