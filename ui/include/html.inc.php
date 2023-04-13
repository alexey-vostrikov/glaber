<?php
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


function italic($str) {
	if (is_array($str)) {
		foreach ($str as $key => $val) {
			if (is_string($val)) {
				$em = new CTag('em', true);
				$em->addItem($val);
				$str[$key] = $em;
			}
		}
	}
	elseif (is_string($str)) {
		$em = new CTag('em', true, '');
		$em->addItem($str);
		$str = $em;
	}
	return $str;
}

function bold($str) {
	if (is_array($str)) {
		foreach ($str as $key => $val) {
			if (is_string($val)) {
				$str[$key] = new CTag('b', true, $val);
			}
		}

		return $str;
	}

	return new CTag('b', true, $str);
}

function make_decoration($haystack, $needle, $class = null) {
	$result = $haystack;

	$tmpHaystack = mb_strtolower($haystack);
	$tmpNeedle = mb_strtolower($needle);
	$pos = mb_strpos($tmpHaystack, $tmpNeedle);

	if ($pos !== false) {
		$start = CHtml::encode(mb_substr($haystack, 0, $pos));
		$end = CHtml::encode(mb_substr($haystack, $pos + mb_strlen($needle)));
		$found = CHtml::encode(mb_substr($haystack, $pos, mb_strlen($needle)));

		if (is_null($class)) {
			$result = [$start, bold($found), $end];
		}
		else {
			$result = [$start, (new CSpan($found))->addClass($class), $end];
		}
	}

	return $result;
}

function prepareUrlParam($value, $name = null) {
	if (is_array($value)) {
		$result = '';

		foreach ($value as $key => $param) {
			$result .= prepareUrlParam($param, isset($name) ? $name.'['.$key.']' : $key);
		}
	}
	else {
		$result = '&'.$name.'='.urlencode($value);
	}

	return $result;
}

/**
 * Get ready for url params.
 *
 * @param mixed  $param				param name or array with data depend from $getFromRequest
 * @param bool   $getFromRequest	detect data source - input array or $_REQUEST variable
 * @param string $name				if $_REQUEST variable is used this variable not used
 *
 * @return string
 */
function url_param($param, $getFromRequest = true, $name = null) {
	if (is_array($param)) {
		if ($getFromRequest) {
			fatal_error(_('URL parameter cannot be array.'));
		}
	}
	else {
		if (is_null($name)) {
			if (!$getFromRequest) {
				fatal_error(_('URL parameter name is empty.'));
			}

			$name = $param;
		}
	}

	if ($getFromRequest) {
		$value =& $_REQUEST[$param];
	}
	else {
		$value =& $param;
	}

	return isset($value) ? prepareUrlParam($value, $name) : '';
}

function url_params(array $params) {
	$result = '';

	foreach ($params as $param) {
		$result .= url_param($param);
	}

	return $result;
}

function BR() {
	return new CTag('br');
}

function get_icon($type, $params = []) {
	switch ($type) {
		case 'favorite':
			if (CFavorite::exists($params['fav'], $params['elid'], $params['elname'])) {
				$icon = (new CRedirectButton(SPACE, null))
					->addClass(ZBX_STYLE_BTN_REMOVE_FAV)
					->setTitle(_('Remove from favorites'))
					->onClick('rm4favorites("'.$params['elname'].'", "'.$params['elid'].'");');
			}
			else {
				$icon = (new CRedirectButton(SPACE, null))
					->addClass(ZBX_STYLE_BTN_ADD_FAV)
					->setTitle(_('Add to favorites'))
					->onClick('add2favorites("'.$params['elname'].'", "'.$params['elid'].'");');
			}
			$icon->setId('addrm_fav');

			return $icon;

		case 'kioskmode':
			if ($params['mode'] == ZBX_LAYOUT_KIOSKMODE) {
				$icon = (new CButton(null, '&nbsp;'))
					->setTitle(_('Normal view'))
					->setAttribute('data-layout-mode', ZBX_LAYOUT_NORMAL)
					->addClass(ZBX_LAYOUT_MODE)
					->addClass(ZBX_STYLE_BTN_DASHBOARD_NORMAL)
					->addClass(ZBX_STYLE_BTN_MIN);
			}
			else {
				$icon = (new CButton(null, '&nbsp;'))
					->setTitle(_('Kiosk mode'))
					->setAttribute('data-layout-mode', ZBX_LAYOUT_KIOSKMODE)
					->addClass(ZBX_LAYOUT_MODE)
					->addClass(ZBX_STYLE_BTN_KIOSK);
			}

			return $icon;
	}
}


/**
 * Get map navigation.
 *
 * @param int    $sysmapid      Used as value for sysmaid in map link generation.
 * @param string $name          Used as label for map link generation.
 * @param int    $severity_min  Used as value for severity_min in map link generation.
 *
 * @return CList
 */
function getSysmapNavigation($sysmapid, $name, $severity_min) {
	$list = (new CList())->addItem(new CBreadcrumbs([
		(new CSpan())->addItem(new CLink(_('All maps'), new CUrl('sysmaps.php'))),
		(new CSpan())
			->addClass(ZBX_STYLE_SELECTED)
			->addItem(new CLink($name,
				(new CUrl('zabbix.php'))
					->setArgument('action', 'map.view')
					->setArgument('sysmapid', $sysmapid)
					->setArgument('severity_min', $severity_min)
			))
	]));

	// get map parent maps
	$parent_sysmaps = get_parent_sysmaps($sysmapid);
	if ($parent_sysmaps) {
		$parent_maps = (new CList())
			->setAttribute('aria-label', _('Upper level maps'))
			->addItem((new CSpan())->addItem(_('Upper level maps').':'));

		foreach ($parent_sysmaps as $parent_sysmap) {
			$parent_maps->addItem((new CSpan())->addItem(new CLink($parent_sysmap['name'],
				(new CUrl('zabbix.php'))
					->setArgument('action', 'map.view')
					->setArgument('sysmapid', $parent_sysmap['sysmapid'])
					->setArgument('severity_min', $severity_min)
			)));
		}

		$list->addItem($parent_maps);
	}

	return $list;
}

/**
 * Renders a form footer with the given buttons.
 *
 * @param CButtonInterface 		$main_button	main button that will be displayed on the left
 * @param CButtonInterface[] 	$other_buttons
 *
 * @return CDiv
 *
 * @throws InvalidArgumentException	if an element of $other_buttons contain something other than CButtonInterface
 */
function makeFormFooter(CButtonInterface $main_button = null, array $other_buttons = []) {
	foreach ($other_buttons as $other_button) {
		$other_button->addClass(ZBX_STYLE_BTN_ALT);
	}

	if ($main_button !== null) {
		array_unshift($other_buttons, $main_button);
	}

	return (new CList())
		->addClass(ZBX_STYLE_TABLE_FORMS)
		->addItem([
			(new CDiv())->addClass(ZBX_STYLE_TABLE_FORMS_TD_LEFT),
			(new CDiv($other_buttons))
				->addClass(ZBX_STYLE_TABLE_FORMS_TD_RIGHT)
				->addClass('tfoot-buttons')
		]);
}

/**
 * Create HTML helper element for host interfaces availability.
 *
 * @param array $host_interfaces
 *
 * @return CHostAvailability
 */
function getHostAvailabilityTable(array $host_interfaces): CHostAvailability {
	$interfaces = [];

	foreach ($host_interfaces as $interface) {
		$description = null;

		if ($interface['type'] == INTERFACE_TYPE_SNMP) {
			$description = getSnmpInterfaceDescription($interface);
		}

		$interfaces[] = [
			'type' => $interface['type'],
			'available' => $interface['available'],
			'interface' => getHostInterface($interface),
			'description' => $description,
			'error' =>  $interface['error'],
			'lastchange' => (isset($interface['lastchange']) ? $interface['lastchange'] : 0) ];
	}

	return (new CHostAvailability())->setInterfaces($interfaces);
}

/**
 * Returns the discovered host group lifetime indicator.
 *
 * @param string $current_time	current Unix timestamp
 * @param array  $ts_delete		deletion timestamp of the host group
 *
 * @return CDiv
 */
function getHostGroupLifetimeIndicator($current_time, $ts_delete) {
	// Check if the element should've been deleted in the past.
	if ($current_time > $ts_delete) {
		$warning = _(
			'The host group is not discovered anymore and will be deleted the next time discovery rule is processed.'
		);
	}
	else {
		$warning = _s(
			'The host group is not discovered anymore and will be deleted in %1$s (on %2$s at %3$s).',
			zbx_date2age($current_time, $ts_delete),
			zbx_date2str(DATE_FORMAT, $ts_delete),
			zbx_date2str(TIME_FORMAT, $ts_delete)
		);
	}

	return makeWarningIcon($warning);
}

/**
 * Returns the discovered host lifetime indicator.
 *
 * @param string $current_time	current Unix timestamp
 * @param array  $ts_delete		deletion timestamp of the host
 *
 * @return CDiv
 */
function getHostLifetimeIndicator($current_time, $ts_delete) {
	// Check if the element should've been deleted in the past.
	if ($current_time > $ts_delete) {
		$warning = _(
			'The host is not discovered anymore and will be deleted the next time discovery rule is processed.'
		);
	}
	else {
		$warning = _s(
			'The host is not discovered anymore and will be deleted in %1$s (on %2$s at %3$s).',
			zbx_date2age($current_time, $ts_delete),
			zbx_date2str(DATE_FORMAT, $ts_delete),
			zbx_date2str(TIME_FORMAT, $ts_delete)
		);
	}

	return makeWarningIcon($warning);
}

/**
 * Returns the discovered graph lifetime indicator.
 *
 * @param string $current_time	current Unix timestamp
 * @param array  $ts_delete		deletion timestamp of the graph
 *
 * @return CDiv
 */
function getGraphLifetimeIndicator($current_time, $ts_delete) {
	// Check if the element should've been deleted in the past.
	if ($current_time > $ts_delete) {
		$warning = _(
			'The graph is not discovered anymore and will be deleted the next time discovery rule is processed.'
		);
	}
	else {
		$warning = _s(
			'The graph is not discovered anymore and will be deleted in %1$s (on %2$s at %3$s).',
			zbx_date2age($current_time, $ts_delete),
			zbx_date2str(DATE_FORMAT, $ts_delete),
			zbx_date2str(TIME_FORMAT, $ts_delete)
		);
	}

	return makeWarningIcon($warning);
}

/**
 * Returns the discovered trigger lifetime indicator.
 *
 * @param string $current_time	current Unix timestamp
 * @param array  $ts_delete		deletion timestamp of the trigger
 *
 * @return CDiv
 */
function getTriggerLifetimeIndicator($current_time, $ts_delete) {
	// Check if the element should've been deleted in the past.
	if ($current_time > $ts_delete) {
		$warning = _(
			'The trigger is not discovered anymore and will be deleted the next time discovery rule is processed.'
		);
	}
	else {
		$warning = _s(
			'The trigger is not discovered anymore and will be deleted in %1$s (on %2$s at %3$s).',
			zbx_date2age($current_time, $ts_delete),
			zbx_date2str(DATE_FORMAT, $ts_delete),
			zbx_date2str(TIME_FORMAT, $ts_delete)
		);
	}

	return makeWarningIcon($warning);
}

/**
 * Returns the discovered item lifetime indicator.
 *
 * @param string $current_time	current Unix timestamp
 * @param array  $ts_delete		deletion timestamp of the item
 *
 * @return CDiv
 */
function getItemLifetimeIndicator($current_time, $ts_delete) {
	// Check if the element should've been deleted in the past.
	if ($current_time > $ts_delete) {
		$warning = _(
			'The item is not discovered anymore and will be deleted the next time discovery rule is processed.'
		);
	}
	else {
		$warning = _s(
			'The item is not discovered anymore and will be deleted in %1$s (on %2$s at %3$s).',
			zbx_date2age($current_time, $ts_delete),
			zbx_date2str(DATE_FORMAT, $ts_delete),
			zbx_date2str(TIME_FORMAT, $ts_delete)
		);
	}

	return makeWarningIcon($warning);
}

function makeServerStatusOutput() {
	return (new CTag('output', true))
		->setId('msg-global-footer')
		->addClass(ZBX_STYLE_MSG_GLOBAL_FOOTER)
		->addClass(ZBX_STYLE_MSG_WARNING);
}

/**
* Make logo of the specified type.
*
* @param int $type  LOGO_TYPE_NORMAL | LOGO_TYPE_SIDEBAR | LOGO_TYPE_SIDEBAR_COMPACT.
*
* @return CTag
*/
function makeLogo(int $type): ?CTag {
	static $zabbix_logo_classes = [
		LOGO_TYPE_NORMAL => ZBX_STYLE_ZABBIX_LOGO,
		LOGO_TYPE_SIDEBAR => ZBX_STYLE_ZABBIX_SIDEBAR_LOGO,
		LOGO_TYPE_SIDEBAR_COMPACT => ZBX_STYLE_ZABBIX_SIDEBAR_LOGO_COMPACT
	];

	$brand_logo = CBrandHelper::getLogo($type);

	if ($brand_logo !== null) {
		return (new CImg($brand_logo));
	}
	else {
		return (new CDiv())->addClass($zabbix_logo_classes[$type]);
	}
}

/**
 * Renders a page footer.
 *
 * @param bool $with_version
 *
 * @return CDiv
 */
function makePageFooter($with_version = true) {
	return (new CTag('footer', true, CBrandHelper::getFooterContent($with_version)))
		->setAttribute('role', 'contentinfo');
}

/**
 * Get drop-down submenu item list for the User settings section.
 *
 * @return array  Menu definition for CHtmlPage::setTitleSubmenu.
 */
function getUserSettingsSubmenu(): array {
	if (!CWebUser::checkAccess(CRoleHelper::ACTIONS_MANAGE_API_TOKENS)) {
		return [];
	}

	$profile_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'userprofile.edit')
		->getUrl();

	$tokens_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'user.token.list')
		->getUrl();

	return [
		'main_section' => [
			'items' => array_filter([
				$profile_url => _('User profile'),
				$tokens_url  => _('API tokens')
			])
		]
	];
}

/**
 * Get drop-down submenu item list for the Administration->General section.
 *
 * @return array  Menu definition for CHtmlPage::setTitleSubmenu.
 */
function getAdministrationGeneralSubmenu() {
	$gui_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'gui.edit')
		->getUrl();

	$autoreg_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'autoreg.edit')
		->getUrl();

	$image_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'image.list')
		->getUrl();

	$iconmap_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'iconmap.list')
		->getUrl();

	$regex_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'regex.list')
		->getUrl();

	$trigdisplay_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'trigdisplay.edit')
		->getUrl();

	$geomap_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'geomaps.edit')
		->getUrl();

	$modules_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'module.list')
		->getUrl();

	$miscconfig_url = (new CUrl('zabbix.php'))
		->setArgument('action', 'miscconfig.edit')
		->getUrl();

	return [
		'main_section' => [
			'items' => array_filter([
				$gui_url            => _('GUI'),
				$autoreg_url        => _('Autoregistration'),
				$image_url          => _('Images'),
				$iconmap_url        => _('Icon mapping'),
				$regex_url          => _('Regular expressions'),
				$trigdisplay_url    => _('Trigger displaying options'),
				$geomap_url			=> _('Geographical maps'),
				$modules_url        => _('Modules'),
				$miscconfig_url     => _('Other')
			])
		]
	];
}

/**
 * Renders an icon list.
 *
 * @param array $info_icons  The list of information icons.
 *
 * @return CDiv|string
 */
function makeInformationList($info_icons) {
	return $info_icons ? (new CDiv($info_icons))->addClass(ZBX_STYLE_REL_CONTAINER) : '';
}

/**
 * Renders an information icon like green [i] with message.
 *
 * @param string $message
 *
 * @return CLink
 */
function makeInformationIcon($message) {
	return (new CLink())
		->addClass(ZBX_STYLE_ICON_INFO)
		->addClass(ZBX_STYLE_STATUS_GREEN)
		->setHint($message, ZBX_STYLE_HINTBOX_WRAP);
}

/**
 * Renders an icon for host in maintenance.
 *
 * @param int    $type         Type of the maintenance.
 * @param string $name         Name of the maintenance.
 * @param string $description  Description of the maintenance.
 *
 * @return CLink
 */
function makeMaintenanceIcon($type, $name, $description) {
	$hint = $name.' ['.($type
		? _('Maintenance without data collection')
		: _('Maintenance with data collection')).']';

	if ($description !== '') {
		$hint .= "\n".$description;
	}

	return (new CLink())
		->addClass(ZBX_STYLE_ICON_MAINTENANCE)
		->setHint($hint);
}

/**
 * Renders an icon for suppressed problem.
 *
 * @param array  $icon_data
 * @param string $icon_data[]['suppress_until']    Time until the problem is suppressed.
 * @param string $icon_data[]['maintenance_name']  Name of the maintenance.
 * @param string $icon_data[]['username']          User who created manual suppression.
 * @param bool   $blink                            Add 'blink' CSS class for jqBlink.
 *
 * @return CLink
 */
function makeSuppressedProblemIcon(array $icon_data, bool $blink = false) {
	$suppress_until_values = array_column($icon_data, 'suppress_until');

	if (in_array(ZBX_PROBLEM_SUPPRESS_TIME_INDEFINITE, $suppress_until_values)) {
		$suppressed_till = _s('Indefinitely');
	}
	else {
		$max_value = max($suppress_until_values);
		$suppressed_till = $max_value < strtotime('tomorrow')
			? zbx_date2str(TIME_FORMAT, $max_value)
			: zbx_date2str(DATE_TIME_FORMAT, $max_value);
	}

	CArrayHelper::sort($icon_data, ['maintenance_name']);

	$maintenance_names = [];
	$username = '';

	foreach ($icon_data as $suppression) {
		if (array_key_exists('maintenance_name', $suppression)) {
			$maintenance_names[] = $suppression['maintenance_name'];
		}
		else {
			$username = $suppression['username'];
		}
	}

	$maintenances = implode(',', $maintenance_names);

	return (new CSimpleButton())
		->addClass(ZBX_STYLE_ACTION_ICON_SUPPRESS)
		->addClass($blink ? 'blink' : null)
		->setHint(
			_s('Suppressed till: %1$s', $suppressed_till).
			"\n".
			($username !== '' ? _s('Manually by: %1$s', $username)."\n" : '').
			($maintenances !== '' ? _s('Maintenance: %1$s', $maintenances)."\n" : '')
		);
}

/**
 * Renders an action icon.
 *
 * @param array  $icon_data
 * @param string $icon_data[icon]    Icon style.
 * @param array  $icon_data[hint]    Hintbox content (optional).
 * @param bool   $icon_data[button]  Use button element (optional).
 * @param int    $icon_data[num]     Number displayed over the icon (optional).
 *
 * @return CTag  Returns CSpan or CButton depending on boolean $icon_data['button'] parameter
 */
function makeActionIcon(array $icon_data): CTag {
	if (array_key_exists('button', $icon_data) && $icon_data['button']) {
		$icon = (new CButton(null))->addClass($icon_data['icon']);
	}
	else {
		$icon = (new CSpan())->addClass($icon_data['icon']);
	}

	if (array_key_exists('num', $icon_data)) {
		if ($icon_data['num'] > 99) {
			$icon_data['num'] = '99+';
		}
		$icon->setAttribute('data-count', $icon_data['num']);
	}

	if (array_key_exists('hint', $icon_data)) {
		$icon
			->addClass(ZBX_STYLE_CURSOR_POINTER)
			->setHint($icon_data['hint'], '', true, 'max-width: '.ZBX_ACTIONS_POPUP_MAX_WIDTH.'px;');
	}
	elseif (array_key_exists('title', $icon_data)) {
		$icon->setTitle($icon_data['title']);
	}

	if (array_key_exists('style', $icon_data)) {
		$icon->addStyle($icon_data['style']);
	}

	if (array_key_exists('aria-label', $icon_data)) {
		$icon
			->addItem($icon_data['aria-label'])
			->addClass(ZBX_STYLE_INLINE_SR_ONLY);
	}

	return $icon;
}

/**
 * Renders an icon for a description.
 *
 * @param string $description
 *
 * @return CLink
 */
function makeDescriptionIcon($description) {
	return (new CLink())
		->addClass(ZBX_STYLE_ICON_DESCRIPTION)
		->setHint(zbx_str2links($description), ZBX_STYLE_HINTBOX_WRAP);
}

/**
 * Renders an error icon like red [i] with error message
 *
 * @param string $error
 *
 * @return CLink
 */
function makeErrorIcon($error) {
	return (new CLink())
		->addClass(ZBX_STYLE_ICON_INFO)
		->addClass(ZBX_STYLE_STATUS_RED)
		->setHint($error, ZBX_STYLE_HINTBOX_WRAP." ".ZBX_STYLE_RED);
}

/**
 * Renders an icon with question mark and text in hint.
 *
 * @param string|array|CTag $help_text
 *
 * @return CLink
 */
function makeHelpIcon($help_text): CLink {
	return (new CLink())
		->addClass(ZBX_STYLE_ICON_HELP_HINT)
		->setHint($help_text, ZBX_STYLE_HINTBOX_WRAP);
}

/**
 * Renders a warning icon like yellow [i] with error message
 *
 * @param string $error
 *
 * @return CLink
 */
function makeWarningIcon($error) {
	return (new CLink())
		->addClass(ZBX_STYLE_ICON_INFO)
		->addClass(ZBX_STYLE_STATUS_YELLOW)
		->setHint($error, ZBX_STYLE_HINTBOX_WRAP);
}

/**
 * Returns css for trigger severity backgrounds.
 *
 * @return string
 */
function getTriggerSeverityCss() {
	$css = '';

	$severities = [
		ZBX_STYLE_NA_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_0),
		ZBX_STYLE_INFO_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_1),
		ZBX_STYLE_WARNING_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_2),
		ZBX_STYLE_AVERAGE_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_3),
		ZBX_STYLE_HIGH_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_4),
		ZBX_STYLE_DISASTER_BG => CSettingsHelper::getGlobal(CSettingsHelper::SEVERITY_COLOR_5)
	];

	$css .= ':root {'."\n";
	foreach ($severities as $class => $color) {
		$css .= '--severity-color-'.$class.': #'.$color.';'."\n";
	}
	$css .= '}'."\n";

	foreach ($severities as $class => $color) {
		$css .= '.'.$class.', .'.$class.' input[type="radio"]:checked + label, .'.$class.':before, .flh-'.$class.
			', .status-'.$class.', .status-'.$class.':before { background-color: #'.$color.' }'."\n";
	}

	return $css;
}

/**
 * Returns css for trigger status colors, if those are customized.
 *
 * @return string
 */
function getTriggerStatusCss() {
	$css = '';

	if (CSettingsHelper::getGlobal(CSettingsHelper::CUSTOM_COLOR) == EVENT_CUSTOM_COLOR_ENABLED) {
		$event_statuses = [
			ZBX_STYLE_PROBLEM_UNACK_FG => CSettingsHelper::get(CSettingsHelper::PROBLEM_UNACK_COLOR),
			ZBX_STYLE_PROBLEM_ACK_FG => CSettingsHelper::get(CSettingsHelper::PROBLEM_ACK_COLOR),
			ZBX_STYLE_OK_UNACK_FG => CSettingsHelper::get(CSettingsHelper::OK_UNACK_COLOR),
			ZBX_STYLE_OK_ACK_FG => CSettingsHelper::get(CSettingsHelper::OK_ACK_COLOR)
		];

		foreach ($event_statuses as $class => $color) {
			$css .= '.' . $class . ' {color: #' . $color . ';}' . "\n";
		}
	}

	return $css;
}
