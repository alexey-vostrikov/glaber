<?php declare(strict_types = 1);
/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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


/**
 * A class designed to perform actions related to check access of roles.
 */
class CRoleHelper {

	public const UI_MONITORING_DASHBOARD = 'ui.monitoring.dashboard';
	public const UI_MONITORING_PROBLEMS = 'ui.monitoring.problems';
	public const UI_MONITORING_HOSTS = 'ui.monitoring.hosts';
	public const UI_MONITORING_OVERVIEW = 'ui.monitoring.overview';
	public const UI_MONITORING_LATEST_DATA = 'ui.monitoring.latest_data';
	public const UI_MONITORING_MAPS = 'ui.monitoring.maps';
	public const UI_MONITORING_DISCOVERY = 'ui.monitoring.discovery';
	public const UI_MONITORING_SERVICES = 'ui.monitoring.services';
	public const UI_INVENTORY_OVERVIEW = 'ui.inventory.overview';
	public const UI_INVENTORY_HOSTS = 'ui.inventory.hosts';
	public const UI_REPORTS_SYSTEM_INFO = 'ui.reports.system_info';
	public const UI_REPORTS_AVAILABILITY_REPORT = 'ui.reports.availability_report';
	public const UI_REPORTS_TOP_TRIGGERS = 'ui.reports.top_triggers';
	public const UI_REPORTS_AUDIT = 'ui.reports.audit';
	public const UI_REPORTS_ACTION_LOG = 'ui.reports.action_log';
	public const UI_REPORTS_NOTIFICATIONS = 'ui.reports.notifications';
	public const UI_REPORTS_SCHEDULED_REPORTS = 'ui.reports.scheduled_reports';
	public const UI_CONFIGURATION_HOST_GROUPS = 'ui.configuration.host_groups';
	public const UI_CONFIGURATION_TEMPLATES = 'ui.configuration.templates';
	public const UI_CONFIGURATION_HOSTS = 'ui.configuration.hosts';
	public const UI_CONFIGURATION_MAINTENANCE = 'ui.configuration.maintenance';
	public const UI_CONFIGURATION_ACTIONS = 'ui.configuration.actions';
	public const UI_CONFIGURATION_EVENT_CORRELATION = 'ui.configuration.event_correlation';
	public const UI_CONFIGURATION_DISCOVERY = 'ui.configuration.discovery';
	public const UI_CONFIGURATION_SERVICES = 'ui.configuration.services';
	public const UI_ADMINISTRATION_GENERAL = 'ui.administration.general';
	public const UI_ADMINISTRATION_PROXIES = 'ui.administration.proxies';
	public const UI_ADMINISTRATION_AUTHENTICATION = 'ui.administration.authentication';
	public const UI_ADMINISTRATION_USER_GROUPS = 'ui.administration.user_groups';
	public const UI_ADMINISTRATION_USER_ROLES = 'ui.administration.user_roles';
	public const UI_ADMINISTRATION_USERS = 'ui.administration.users';
	public const UI_ADMINISTRATION_MEDIA_TYPES = 'ui.administration.media_types';
	public const UI_ADMINISTRATION_SCRIPTS = 'ui.administration.scripts';
	public const UI_ADMINISTRATION_QUEUE = 'ui.administration.queue';
	public const UI_DEFAULT_ACCESS = 'ui.default_access';
	public const MODULES_MODULE = 'modules.module.';
	public const MODULES_DEFAULT_ACCESS = 'modules.default_access';
	public const API_ACCESS = 'api.access';
	public const API_ACCESS_DISABLED = 0;
	public const API_ACCESS_ENABLED = 1;
	public const API_MODE = 'api.mode';
	public const API_MODE_DENY = 0;
	public const API_MODE_ALLOW = 1;
	public const API_METHOD = 'api.method.';
	public const ACTIONS_EDIT_DASHBOARDS = 'actions.edit_dashboards';
	public const ACTIONS_EDIT_MAPS = 'actions.edit_maps';
	public const ACTIONS_EDIT_MAINTENANCE = 'actions.edit_maintenance';
	public const ACTIONS_ADD_PROBLEM_COMMENTS = 'actions.add_problem_comments';
	public const ACTIONS_CHANGE_SEVERITY = 'actions.change_severity';
	public const ACTIONS_ACKNOWLEDGE_PROBLEMS = 'actions.acknowledge_problems';
	public const ACTIONS_CLOSE_PROBLEMS = 'actions.close_problems';
	public const ACTIONS_EXECUTE_SCRIPTS = 'actions.execute_scripts';
	public const ACTIONS_MANAGE_API_TOKENS = 'actions.manage_api_tokens';
	public const ACTIONS_MANAGE_SCHEDULED_REPORTS = 'actions.manage_scheduled_reports';
	public const ACTIONS_DEFAULT_ACCESS = 'actions.default_access';
	public const DEFAULT_ACCESS_DISABLED = 0;
	public const DEFAULT_ACCESS_ENABLED = 1;

	public const SECTION_UI = 'ui';
	public const SECTION_MODULES = 'modules';
	public const SECTION_API = 'api';
	public const SECTION_ACTIONS = 'actions';

	public const UI_SECTION_MONITORING = 'ui.monitoring';
	public const UI_SECTION_INVENTORY = 'ui.inventory';
	public const UI_SECTION_REPORTS = 'ui.reports';
	public const UI_SECTION_CONFIGURATION = 'ui.configuration';
	public const UI_SECTION_ADMINISTRATION = 'ui.administration';

	public const API_WILDCARD = '*';
	public const API_WILDCARD_ALIAS = '*.*';
	public const API_ANY_METHOD = '.*';
	public const API_ANY_SERVICE = '*.';

	/**
	 * Array for storing roles data (including rules) loaded from Role API object and converted to one format. The data
	 * of specific role can be accessed in following way: self::roles[{role ID}].
	 *
	 * @static
	 *
	 * @var array
	 */
	private static $roles = [];

	/**
	 * Array for storing all API methods by user type.
	 *
	 * @var array
	 */
	private static $api_methods = [];

	/**
	 * Array for storing all API methods with masks by user type.
	 *
	 * @var array
	 */
	private static $api_method_masks = [];

	/**
	 * Checks the access of specific role to specific rule.
	 *
	 * @static
	 *
	 * @param string  $rule_name  Name of the rule to check access for.
	 * @param integer $roleid     ID of the role where check of access is necessary to perform.
	 *
	 * @return bool  Returns true if role have access to specified rule, false - otherwise.
	 */
	public static function checkAccess(string $rule_name, $roleid): bool {
		self::loadRoleRules($roleid);

		if (!array_key_exists($rule_name, self::$roles[$roleid]['rules']) || $rule_name === self::SECTION_API) {
			return false;
		}

		return self::$roles[$roleid]['rules'][$rule_name];
	}

	/**
	 * Check rule can be defined for specific role type.
	 *
	 * @param string $rule_name  Rule full name, with section prefix.
	 * @param int    $role_type  Role access type.
	 *
	 * @return bool
	 */
	public static function checkRuleAllowedByType($rule_name, int $role_type): bool {
		$allowed = [
			self::UI_DEFAULT_ACCESS, self::API_ACCESS, self::API_MODE, self::MODULES_DEFAULT_ACCESS,
			self::ACTIONS_DEFAULT_ACCESS
		];

		if (in_array($rule_name, $allowed)) {
			return true;
		}

		switch (self::getRuleSection($rule_name)) {
			case self::SECTION_UI:
				$allowed = self::getAllUiElements($role_type);
				break;

			case self::SECTION_API:
				$allowed = self::getApiMethods($role_type);
				break;

			case self::SECTION_MODULES:
				$allowed = [$rule_name];
				break;

			case self::SECTION_ACTIONS:
				$allowed = self::getAllActions($role_type);
				break;
		}

		return in_array($rule_name, $allowed);
	}

	/**
	 * Gets list of API methods (with wildcards if that exists) that are considered allowed or denied (depending from
	 * API access mode) for specific role.
	 *
	 * @static
	 *
	 * @param integer $roleid  Role ID.
	 *
	 * @return array  Returns the array of API methods.
	 */
	public static function getRoleApiMethods(int $roleid): array {
		self::loadRoleRules($roleid);

		return self::$roles[$roleid]['rules'][self::SECTION_API];
	}

	/**
	 * Loads once all rules of specified Role API object by ID and converts rule data to one format.
	 *
	 * @static
	 *
	 * @throws Exception
	 *
	 * @param integer $roleid  Role ID.
	 */
	private static function loadRoleRules($roleid): void {
		if (array_key_exists($roleid, self::$roles)) {
			return;
		}

		$roles = API::Role()->get([
			'output' => ['roleid', 'name', 'type'],
			'selectRules' => ['ui', 'ui.default_access', 'modules', 'modules.default_access', 'api.access', 'api.mode',
				'api', 'actions', 'actions.default_access'
			],
			'roleids' => $roleid
		]);

		if ($roles === false) {
			throw new Exception(_('Specified role was not found.'));
		}

		$role = $roles[0];

		$rules = [
			self::UI_DEFAULT_ACCESS => (bool) $role['rules'][self::UI_DEFAULT_ACCESS],
			self::MODULES_DEFAULT_ACCESS => (bool) $role['rules'][self::MODULES_DEFAULT_ACCESS],
			self::API_ACCESS => (bool) $role['rules'][self::API_ACCESS],
			self::API_MODE => (bool) $role['rules'][self::API_MODE],
			self::SECTION_API => $role['rules'][self::SECTION_API],
			self::ACTIONS_DEFAULT_ACCESS => (bool) $role['rules'][self::ACTIONS_DEFAULT_ACCESS]
		];

		foreach ($role['rules'][self::SECTION_UI] as $rule) {
			$rules[self::SECTION_UI.'.'.$rule['name']] = (bool) $rule['status'];
		}

		foreach ($role['rules'][self::SECTION_MODULES] as $module) {
			$rules[self::MODULES_MODULE.$module['moduleid']] = (bool) $module['status'];
		}

		foreach ($role['rules'][self::SECTION_ACTIONS] as $rule) {
			$rules[self::SECTION_ACTIONS.'.'.$rule['name']] = (bool) $rule['status'];
		}

		$role['type'] = (int) $role['type'];
		$role['rules'] = $rules;

		self::$roles[$roleid] = $role;
	}

	/**
	 * Gets the section name of specific rule name.
	 *
	 * @static
	 *
	 * @throws Exception
	 *
	 * @param string $rule_name  Rule name.
	 *
	 * @return array Returns name of rules section.
	 */
	public static function getRuleSection(string $rule_name): string {
		$section = explode('.', $rule_name, 2)[0];
		if (in_array($section, [self::SECTION_UI, self::SECTION_MODULES, self::SECTION_API, self::SECTION_ACTIONS])) {
			return $section;
		}

		throw new Exception(_('Rule section was not found.'));
	}

	/**
	 * Gets all available UI elements rules for specific user type.
	 *
	 * @static
	 *
	 * @param integer $user_type  User type.
	 *
	 * @return array  Returns the array of rule names for specified user type.
	 */
	public static function getAllUiElements(int $user_type): array {
		$rules = [
			self::UI_MONITORING_DASHBOARD, self::UI_MONITORING_PROBLEMS, self::UI_MONITORING_HOSTS,
			self::UI_MONITORING_OVERVIEW, self::UI_MONITORING_LATEST_DATA, self::UI_MONITORING_MAPS,
			self::UI_MONITORING_SERVICES, self::UI_INVENTORY_OVERVIEW, self::UI_INVENTORY_HOSTS,
			self::UI_REPORTS_AVAILABILITY_REPORT, self::UI_REPORTS_TOP_TRIGGERS
		];

		if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
			$rules = array_merge($rules, [
				self::UI_MONITORING_DISCOVERY, self::UI_REPORTS_NOTIFICATIONS, self::UI_REPORTS_SCHEDULED_REPORTS,
				self::UI_CONFIGURATION_HOST_GROUPS, self::UI_CONFIGURATION_TEMPLATES, self::UI_CONFIGURATION_HOSTS,
				self::UI_CONFIGURATION_MAINTENANCE, self::UI_CONFIGURATION_ACTIONS, self::UI_CONFIGURATION_DISCOVERY,
				self::UI_CONFIGURATION_SERVICES
			]);
		}

		if ($user_type === USER_TYPE_SUPER_ADMIN) {
			$rules = array_merge($rules, [
				self::UI_REPORTS_SYSTEM_INFO, self::UI_REPORTS_AUDIT, self::UI_REPORTS_ACTION_LOG,
				self::UI_CONFIGURATION_EVENT_CORRELATION, self::UI_ADMINISTRATION_GENERAL, self::UI_ADMINISTRATION_PROXIES,
				self::UI_ADMINISTRATION_AUTHENTICATION, self::UI_ADMINISTRATION_USER_GROUPS,
				self::UI_ADMINISTRATION_USER_ROLES, self::UI_ADMINISTRATION_USERS, self::UI_ADMINISTRATION_MEDIA_TYPES,
				self::UI_ADMINISTRATION_SCRIPTS, self::UI_ADMINISTRATION_QUEUE
			]);
		}

		return $rules;
	}

	/**
	 * Gets all available actions rules for specific user type.
	 *
	 * @static
	 *
	 * @param integer $user_type  User type.
	 *
	 * @return array  Returns the array of rule names for specified user type.
	 */
	public static function getAllActions(int $user_type): array {
		$rules = [
			self::ACTIONS_EDIT_DASHBOARDS, self::ACTIONS_EDIT_MAPS, self::ACTIONS_ACKNOWLEDGE_PROBLEMS,
			self::ACTIONS_CLOSE_PROBLEMS, self::ACTIONS_CHANGE_SEVERITY, self::ACTIONS_ADD_PROBLEM_COMMENTS,
			self::ACTIONS_EXECUTE_SCRIPTS, self::ACTIONS_MANAGE_API_TOKENS
		];

		if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
			$rules[] = self::ACTIONS_EDIT_MAINTENANCE;
			$rules[] = self::ACTIONS_MANAGE_SCHEDULED_REPORTS;
		}

		return $rules;
	}

	/**
	 * Gets labels of all available UI sections for specific user type in order as it need to display in UI.
	 *
	 * @static
	 *
	 * @param integer $user_type  User type.
	 *
	 * @return array  Returns the array where key of each element is UI section name and value is UI section label.
	 */
	public static function getUiSectionsLabels(int $user_type): array {
		$sections = [
			self::UI_SECTION_MONITORING => _('Monitoring'),
			self::UI_SECTION_INVENTORY => _('Inventory'),
			self::UI_SECTION_REPORTS => _('Reports')
		];

		if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
			$sections += [self::UI_SECTION_CONFIGURATION => _('Configuration')];
		}

		if ($user_type === USER_TYPE_SUPER_ADMIN) {
			$sections += [self::UI_SECTION_ADMINISTRATION => _('Administration')];
		}

		return $sections;
	}

	/**
	 * Gets labels of all available rules for specific UI section and user type in order as it need to display in UI.
	 *
	 * @static
	 *
	 * @param string  $ui_section_name  UI section name.
	 * @param integer $user_type        User type.
	 *
	 * @return array  Returns the array where key of each element is rule name and value is rule label.
	 */
	public static function getUiSectionRulesLabels(string $ui_section_name, int $user_type): array {
		switch ($ui_section_name) {
			case self::UI_SECTION_MONITORING:
				$labels = [
					self::UI_MONITORING_DASHBOARD => _('Dashboard'),
					self::UI_MONITORING_PROBLEMS => _('Problems'),
					self::UI_MONITORING_HOSTS => _('Hosts'),
					self::UI_MONITORING_OVERVIEW => _('Overview'),
					self::UI_MONITORING_LATEST_DATA => _('Latest data'),
					self::UI_MONITORING_MAPS => _('Maps')
				];

				if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [self::UI_MONITORING_DISCOVERY => _('Discovery')];
				}

				$labels += [self::UI_MONITORING_SERVICES => _('Services')];

				return $labels;
			case self::UI_SECTION_INVENTORY:
				return [
					self::UI_INVENTORY_OVERVIEW => _('Overview'),
					self::UI_INVENTORY_HOSTS => _('Hosts')
				];
			case self::UI_SECTION_REPORTS:
				$labels = [];

				if ($user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [self::UI_REPORTS_SYSTEM_INFO => _('System information')];
				}

				$labels += [
					self::UI_REPORTS_AVAILABILITY_REPORT => _('Availability report'),
					self::UI_REPORTS_TOP_TRIGGERS => _('Triggers top 100')
				];

				if ($user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [
						self::UI_REPORTS_AUDIT => _('Audit'),
						self::UI_REPORTS_ACTION_LOG => _('Action log')
					];
				}

				if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [
						self::UI_REPORTS_NOTIFICATIONS => _('Notifications'),
						self::UI_REPORTS_SCHEDULED_REPORTS => _('Scheduled reports')
					];
				}

				return $labels;
			case self::UI_SECTION_CONFIGURATION:
				$labels = [];

				if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
					$labels = [
						self::UI_CONFIGURATION_HOST_GROUPS => _('Host groups'),
						self::UI_CONFIGURATION_TEMPLATES => _('Templates'),
						self::UI_CONFIGURATION_HOSTS => _('Hosts'),
						self::UI_CONFIGURATION_MAINTENANCE => _('Maintenance'),
						self::UI_CONFIGURATION_ACTIONS => _('Actions')
					];
				}

				if ($user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [self::UI_CONFIGURATION_EVENT_CORRELATION => _('Event correlation')];
				}

				if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
					$labels += [
						self::UI_CONFIGURATION_DISCOVERY => _('Discovery'),
						self::UI_CONFIGURATION_SERVICES => _('Services')
					];
				}

				return $labels;
			case self::UI_SECTION_ADMINISTRATION:
				$labels = [];

				if ($user_type === USER_TYPE_SUPER_ADMIN) {
					$labels = [
						self::UI_ADMINISTRATION_GENERAL => _('General'),
						self::UI_ADMINISTRATION_PROXIES => _('Proxies'),
						self::UI_ADMINISTRATION_AUTHENTICATION => _('Authentication'),
						self::UI_ADMINISTRATION_USER_GROUPS => _('User groups'),
						self::UI_ADMINISTRATION_USER_ROLES => _('User roles'),
						self::UI_ADMINISTRATION_USERS => _('Users'),
						self::UI_ADMINISTRATION_MEDIA_TYPES => _('Media types'),
						self::UI_ADMINISTRATION_SCRIPTS => _('Scripts'),
						self::UI_ADMINISTRATION_QUEUE => _('Queue')
					];
				}

				return $labels;
			default:
				return [];
		}
	}

	/**
	 * Gets labels of all available actions rules for specific user type in order as it need to display on user roles
	 * page.
	 *
	 * @static
	 *
	 * @param integer $user_type  User type.
	 *
	 * @return array  Returns the array where key of each element is rule name and value is rule label.
	 */
	public static function getActionsLabels(int $user_type): array {
		$labels = [
			self::ACTIONS_EDIT_DASHBOARDS => _('Create and edit dashboards'),
			self::ACTIONS_EDIT_MAPS => _('Create and edit maps')
		];

		if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
			$labels += [self::ACTIONS_EDIT_MAINTENANCE => _('Create and edit maintenance')];
		}

		$labels += [
			self::ACTIONS_ADD_PROBLEM_COMMENTS => _('Add problem comments'),
			self::ACTIONS_CHANGE_SEVERITY => ('Change severity'),
			self::ACTIONS_ACKNOWLEDGE_PROBLEMS => _('Acknowledge problems'),
			self::ACTIONS_CLOSE_PROBLEMS => _('Close problems'),
			self::ACTIONS_EXECUTE_SCRIPTS => _('Execute scripts'),
			self::ACTIONS_MANAGE_API_TOKENS => _('Manage API tokens')
		];

		if ($user_type === USER_TYPE_ZABBIX_ADMIN || $user_type === USER_TYPE_SUPER_ADMIN) {
			$labels += [self::ACTIONS_MANAGE_SCHEDULED_REPORTS => _('Manage scheduled reports')];
		}

		return $labels;
	}

	/**
	 * Returns a list of all API methods by user type or API methods available only for the given user type.
	 *
	 * @static
	 *
	 * @param int|null $user_type
	 *
	 * @return array
	 */
	public static function getApiMethods(?int $user_type = null): array {
		if (!self::$api_methods) {
			self::loadApiMethods();
		}

		return ($user_type !== null) ? self::$api_methods[$user_type] : self::$api_methods;
	}

	/**
	 * Returns a list of API methods with masks for the given user type.
	 *
	 * @static
	 *
	 * @param int|null $user_type
	 *
	 * @return array
	 */
	public static function getApiMethodMasks(?int $user_type = null): array {
		if (!self::$api_method_masks) {
			self::loadApiMethods();
		}

		return ($user_type !== null) ? self::$api_method_masks[$user_type] : self::$api_method_masks;
	}

	/**
	 * Returns a list of API methods for each method mask for the given user type.
	 *
	 * @static
	 *
	 * @param int $user_type
	 *
	 * @return array
	 */
	public static function getApiMaskMethods(int $user_type): array {
		$api_methods = self::getApiMethods($user_type);
		$result = [self::API_WILDCARD => $api_methods, self::API_WILDCARD_ALIAS => $api_methods];

		foreach ($api_methods as &$api_method) {
			[$service, $method] = explode('.', $api_method, 2);
			$result[$service.self::API_ANY_METHOD][] = $api_method;
			$result[self::API_ANY_SERVICE.$method][] = $api_method;
		}
		unset($api_method);

		return $result;
	}

	/**
	 * Collects all API methods for all user types.
	 *
	 * @static
	 */
	private static function loadApiMethods(): void {
		$api_methods = [
			USER_TYPE_ZABBIX_USER => [],
			USER_TYPE_ZABBIX_ADMIN => [],
			USER_TYPE_SUPER_ADMIN => []
		];
		$api_method_masks = $api_methods;

		foreach (CApiServiceFactory::API_SERVICES as $service => $class_name) {
			foreach (constant($class_name.'::ACCESS_RULES') as $method => $rules) {
				if ($method === 'validateoperationsintegrity') {
					continue;
				}

				if (array_key_exists('min_user_type', $rules)) {
					switch ($rules['min_user_type']) {
						case USER_TYPE_ZABBIX_USER:
							$api_methods[USER_TYPE_ZABBIX_USER][] = $service.'.'.$method;
							$api_method_masks[USER_TYPE_ZABBIX_USER][$service.self::API_ANY_METHOD] = true;
							$api_method_masks[USER_TYPE_ZABBIX_USER][self::API_ANY_SERVICE.$method] = true;
							// break; is not missing here
						case USER_TYPE_ZABBIX_ADMIN:
							$api_methods[USER_TYPE_ZABBIX_ADMIN][] = $service.'.'.$method;
							$api_method_masks[USER_TYPE_ZABBIX_ADMIN][$service.self::API_ANY_METHOD] = true;
							$api_method_masks[USER_TYPE_ZABBIX_ADMIN][self::API_ANY_SERVICE.$method] = true;
							// break; is not missing here
						case USER_TYPE_SUPER_ADMIN:
							$api_methods[USER_TYPE_SUPER_ADMIN][] = $service.'.'.$method;
							$api_method_masks[USER_TYPE_SUPER_ADMIN][$service.self::API_ANY_METHOD] = true;
							$api_method_masks[USER_TYPE_SUPER_ADMIN][self::API_ANY_SERVICE.$method] = true;
					}
				}
			}
		}

		foreach ($api_method_masks as $user_type => $masks) {
			$api_method_masks[$user_type] = array_merge([self::API_WILDCARD, self::API_WILDCARD_ALIAS],
				array_keys($masks)
			);
		}

		self::$api_methods = $api_methods;
		self::$api_method_masks = $api_method_masks;
	}
}
