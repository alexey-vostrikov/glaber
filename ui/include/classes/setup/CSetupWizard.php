<?php
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
 * Setup wizard form.
 */
class CSetupWizard extends CForm {

	const VAULT_URL_DEFAULT = 'https://localhost:8200';

	protected $DISABLE_CANCEL_BUTTON = false;

	protected $DISABLE_BACK_BUTTON = false;

	protected $SHOW_RETRY_BUTTON = false;

	protected $STEP_FAILED = false;

	protected $frontend_setup;

	function __construct() {

		$this->frontend_setup = new CFrontendSetup();

		$this->stage = [
			0 => [
				'title' => _('Welcome'),
				'fnc' => 'stage0'
			],
			1 => [
				'title' => _('Check of pre-requisites'),
				'fnc' => 'stage1'
			],
			2 => [
				'title' => _('Configure DB connection'),
				'fnc' => 'stage2'
			],
			3 => [
				'title' => _('Server details'),
				'fnc' => 'stage3'
			],
			4 => [
				'title' => _('GUI settings'),
				'fnc' => 'stage4'
			],
			5 => [
				'title' => _('Pre-installation summary'),
				'fnc' => 'stage5'
			],
			6 => [
				'title' => _('Install'),
				'fnc' => 'stage6'
			]
		];

		$this->eventHandler();

		parent::__construct('post');
		parent::setId('setup-form');
	}

	function getConfig($name, $default = null) {
		return CSessionHelper::has($name) ? CSessionHelper::get($name) : $default;
	}

	function setConfig($name, $value) {
		CSessionHelper::set($name, $value);
	}

	/**
	 * Unset given keys from session storage.
	 *
	 * @param array $keys  List of keys to unset.
	 */
	protected function unsetConfig(array $keys): void {
		CSessionHelper::unset($keys);
	}

	private function getStep(): int {
		return $this->getConfig('step', 0);
	}

	private function doNext(): bool {
		if (isset($this->stage[$this->getStep() + 1])) {
			$this->setConfig('step', $this->getStep('step') + 1);

			return true;
		}

		return false;
	}

	private function doBack(): bool {
		if (isset($this->stage[$this->getStep() - 1])) {
			$this->setConfig('step', $this->getStep('step') - 1);

			return true;
		}

		return false;
	}

	protected function bodyToString($destroy = true) {
		$setup_right = (new CDiv($this->getStage()))->addClass(ZBX_STYLE_SETUP_RIGHT);

		$setup_left = (new CDiv())
			->addClass(ZBX_STYLE_SETUP_LEFT)
			->addItem((new CDiv(makeLogo(LOGO_TYPE_NORMAL)))->addClass('setup-logo'))
			->addItem($this->getList());

		if (CWebUser::$data && CWebUser::getType() == USER_TYPE_SUPER_ADMIN) {
			$cancel_button = (new CSubmit('cancel', _('Cancel')))
				->addClass(ZBX_STYLE_BTN_ALT)
				->addClass(ZBX_STYLE_FLOAT_LEFT);
			if ($this->DISABLE_CANCEL_BUTTON) {
				$cancel_button->setEnabled(false);
			}
		}
		else {
			$cancel_button = null;
		}

		if (array_key_exists($this->getStep() + 1, $this->stage)) {
			$next_button = new CSubmit('next['.$this->getStep().']', _('Next step'));
		}
		else {
			$next_button = new CSubmit($this->SHOW_RETRY_BUTTON ? 'retry' : 'finish', _('Finish'));
		}

		$back_button = (new CSubmit('back['.$this->getStep().']', _('Back')))
			->addClass(ZBX_STYLE_BTN_ALT)
			->addClass(ZBX_STYLE_FLOAT_LEFT);

		if ($this->getStep() == 0 || $this->DISABLE_BACK_BUTTON) {
			$back_button->setEnabled(false);
		}

		$setup_footer = (new CDiv([new CDiv([$next_button, $back_button]), $cancel_button]))
			->addClass(ZBX_STYLE_SETUP_FOOTER);

		$setup_container = (new CDiv([$setup_left, $setup_right, $setup_footer]))->addClass(ZBX_STYLE_SETUP_CONTAINER);

		return parent::bodyToString($destroy).$setup_container->toString();
	}

	private function getList(): CList {
		$list = new CList();

		foreach ($this->stage as $id => $data) {
			$list->addItem($data['title'], ($id <= $this->getStep()) ? ZBX_STYLE_SETUP_LEFT_CURRENT : null);
		}

		return $list;
	}

	private function getStage(): array {
		$function = $this->stage[$this->getStep()]['fnc'];
		return $this->$function();
	}

	private function stage0(): array {
		preg_match('/^\d+\.\d+/', ZABBIX_VERSION, $version);
		$setup_title = (new CDiv([new CSpan(_('Welcome to')), 'Glaber '.GLABER_VERSION]))->addClass(ZBX_STYLE_SETUP_TITLE);

		$default_lang = $this->getConfig('default_lang');
		$lang_select = (new CSelect('default_lang'))
			->setId('default-lang')
			->setValue($default_lang)
			->setFocusableElementId('label-default-lang')
			->setAttribute('autofocus', 'autofocus');

		$all_locales_available = 1;

		foreach (getLocales() as $localeid => $locale) {
			if (!$locale['display']) {
				continue;
			}

			/*
			 * Checking if this locale exists in the system. The only way of doing it is to try and set one
			 * trying to set only the LC_MONETARY locale to avoid changing LC_NUMERIC.
			 */
			$locale_available = ($localeid === ZBX_DEFAULT_LANG
					|| setlocale(LC_MONETARY, zbx_locale_variants($localeid))
			);

			$lang_select->addOption((new CSelectOption($localeid, $locale['name']))->setDisabled(!$locale_available));

			$all_locales_available &= (int) $locale_available;
		}

		// Restoring original locale.
		setlocale(LC_MONETARY, zbx_locale_variants($default_lang));

		$language_error = '';
		if (!function_exists('bindtextdomain')) {
			$language_error = 'Translations are unavailable because the PHP gettext module is missing.';
			$lang_select->setReadonly();
		}
		elseif ($all_locales_available == 0) {
			$language_error = _('You are not able to choose some of the languages, because locales for them are not installed on the web server.');
		}

		$language_error = ($language_error !== '')
			? (makeErrorIcon($language_error))->addStyle('margin-left: 5px;')
			: null;

		$language_select = (new CFormList())
			->addRow(new CLabel(_('Default language'), $lang_select->getFocusableElementId()), [
				$lang_select, $language_error
			]);

		return [(new CDiv([$setup_title, $language_select]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)];
	}

	private function stage1(): array {
		$table = (new CTable())
			->addClass(ZBX_STYLE_LIST_TABLE)
			->setHeader(['', _('Current value'), _('Required'), '']);

		$messages = [];
		$finalResult = CFrontendSetup::CHECK_OK;

		foreach ($this->frontend_setup->checkRequirements() as $req) {
			if ($req['result'] == CFrontendSetup::CHECK_OK) {
				$class = ZBX_STYLE_GREEN;
				$result = 'OK';
			}
			elseif ($req['result'] == CFrontendSetup::CHECK_WARNING) {
				$class = ZBX_STYLE_ORANGE;
				$result = new CSpan(_x('Warning', 'setup'));
			}
			else {
				$class = ZBX_STYLE_RED;
				$result = new CSpan(_('Fail'));
				$messages[] = ['type' => 'error', 'message' => $req['error']];
			}

			$table->addRow(
				[
					$req['name'],
					$req['current'],
					($req['required'] !== null) ? $req['required'] : '',
					(new CCol($result))->addClass($class)
				]
			);

			if ($req['result'] > $finalResult) {
				$finalResult = $req['result'];
			}
		}

		if ($finalResult == CFrontendSetup::CHECK_FATAL) {
			$message_box = makeMessageBox(false, $messages, null, false, true);
		}
		else {
			$message_box = null;
		}

		return [
			new CTag('h1', true, _('Check of pre-requisites')),
			(new CDiv([$message_box, $table]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function stage2(): array {
		$DB['TYPE'] = $this->getConfig('DB_TYPE', key(CFrontendSetup::getSupportedDatabases()));

		$table = (new CFormList())
			->addItem([
				(new CVar('tls_encryption', 0))->removeId(),
				(new CVar('verify_certificate', 0))->removeId(),
				(new CVar('verify_host', 0))->removeId()
			]);

		$table->addRow(new CLabel(_('Database type'), 'label-type'),
			(new CSelect('type'))
				->setId('type')
				->setFocusableElementId('label-type')
				->setValue($DB['TYPE'])
				->addOptions(CSelect::createOptionsFromArray(CFrontendSetup::getSupportedDatabases()))
		);

		$table->addRow(_('Database host'),
			(new CTextBox('server', $this->getConfig('DB_SERVER', 'localhost')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
		);

		$table->addRow(_('Database port'), [
			(new CNumericBox('port', $this->getConfig('DB_PORT', '0'), 5, false, false, false))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH),
			(new CDiv())->addClass(ZBX_STYLE_FORM_INPUT_MARGIN),
			(new CSpan(_('0 - use default port')))->addClass(ZBX_STYLE_GREY)
		]);

		$table->addRow(_('Database name'),
			(new CTextBox('database', $this->getConfig('DB_DATABASE', 'zabbix')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
		);

		$table->addRow(_('Database schema'),
			(new CTextBox('schema', $this->getConfig('DB_SCHEMA', '')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH),
			'db_schema_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$db_creds_storage = (int) $this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG);

		$table->addRow(_('Store credentials in'),
			(new CRadioButtonList('creds_storage', $db_creds_storage))
				->addValue(_('Plain text'), DB_STORE_CREDS_CONFIG)
				->addValue(_('HashiCorp Vault'), DB_STORE_CREDS_VAULT)
				->setModern(true)
		);

		$table->addRow(_('Vault API endpoint'),
			(new CTextBox('vault_url', $this->getConfig('DB_VAULT_URL', self::VAULT_URL_DEFAULT)))
				->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH),
			'vault_url_row',
			($db_creds_storage == DB_STORE_CREDS_VAULT) ? ZBX_STYLE_DISPLAY_NONE : null
		);

		$table->addRow(_('Vault secret path'),
			(new CTextBox('vault_db_path', $this->getConfig('DB_VAULT_DB_PATH')))
				->setAttribute('placeholder', _('path/to/secret'))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH),
			'vault_db_path_row',
			($db_creds_storage == DB_STORE_CREDS_VAULT) ? ZBX_STYLE_DISPLAY_NONE : null
		);

		$table->addRow(_('Vault authentication token'),
			(new CTextBox('vault_token', $this->getConfig('DB_VAULT_TOKEN')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
				->setAttribute('maxlength', 2048),
			'vault_token_row',
			($db_creds_storage == DB_STORE_CREDS_VAULT) ? ZBX_STYLE_DISPLAY_NONE : null
		);

		$table->addRow(_('User'),
			(new CTextBox('user', $this->getConfig('DB_USER', 'zabbix')))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH),
			'db_user',
			($db_creds_storage == DB_STORE_CREDS_CONFIG) ? ZBX_STYLE_DISPLAY_NONE : null
		);

		$table->addRow(_('Password'),
			(new CPassBox('password', $this->getConfig('DB_PASSWORD')))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH),
			'db_password',
			($db_creds_storage == DB_STORE_CREDS_CONFIG) ? ZBX_STYLE_DISPLAY_NONE : null
		);

		$table->addRow(_('Database TLS encryption'), [
				(new CCheckBox('tls_encryption'))->setChecked($this->getConfig('DB_ENCRYPTION', true)),
				(new CDiv(
					_('Connection will not be encrypted because it uses a socket file (on Unix) or shared memory (Windows).')
				))
					->setId('tls_encryption_hint')
					->addClass(ZBX_STYLE_DISPLAY_NONE)
			],
			'db_encryption_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow(_('Verify database certificate'),
			(new CCheckBox('verify_certificate'))->setChecked($this->getConfig('DB_ENCRYPTION_ADVANCED')),
			'db_verify_host',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow((new CLabel(_('Database TLS CA file')))->setAsteriskMark(),
			(new CTextBox('ca_file', $this->getConfig('DB_CA_FILE')))->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH),
			'db_cafile_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow(_('Database TLS key file'),
			(new CTextBox('key_file', $this->getConfig('DB_KEY_FILE')))->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH),
			'db_keyfile_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow(_('Database TLS certificate file'),
			(new CTextBox('cert_file', $this->getConfig('DB_CERT_FILE')))->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH),
			'db_certfile_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow(_('Database host verification'),
			(new CCheckBox('verify_host'))->setChecked($this->getConfig('DB_VERIFY_HOST')),
			'db_verify_host_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		$table->addRow(_('Database TLS cipher list'),
			(new CTextBox('cipher_list', $this->getConfig('DB_CIPHER_LIST')))->setWidth(ZBX_TEXTAREA_MEDIUM_WIDTH),
			'db_cipher_row',
			ZBX_STYLE_DISPLAY_NONE
		);

		if ($this->STEP_FAILED) {
			$message_box = makeMessageBox(false, CMessageHelper::getMessages(), _('Cannot connect to the database.'),
				false, true);
		}
		else {
			$message_box = null;
		}

		return [
			new CTag('h1', true, _('Configure DB connection')),
			(new CDiv([
				new CTag('p', true, _s('Please create database manually, and set the configuration parameters for connection to this database. Press "%1$s" button when done.', _('Next step'))),
				$message_box,
				$table
			]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function stage3(): array {
		$table = new CFormList();

		$table->addRow(_('Host'),
			(new CTextBox('zbx_server', $this->getConfig('ZBX_SERVER', 'localhost')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
		);

		$table->addRow(_('Port'),
			(new CNumericBox('zbx_server_port', $this->getConfig('ZBX_SERVER_PORT', '10051'), 5, false, false, false))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
		);

		$table->addRow('Name',
			(new CTextBox('zbx_server_name', $this->getConfig('ZBX_SERVER_NAME', '')))
				->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
		);

		return [
			new CTag('h1', true, _('Zabbix server details')),
			(new CDiv([
				new CTag('p', true, _('Please enter the host name or host IP address and port number of the Zabbix server, as well as the name of the installation (optional).')),
				$table
			]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function stage4(): array {
		$timezones[ZBX_DEFAULT_TIMEZONE] = CDateTimeZoneHelper::getSystemDateTimeZone();
		$timezones += (new CDateTimeZoneHelper())->getAllDateTimeZones();

		$table = (new CFormList())
			->addRow(new CLabel(_('Default time zone'), 'label-default-timezone'),
				(new CSelect('default_timezone'))
					->setValue($this->getConfig('default_timezone', ZBX_DEFAULT_TIMEZONE))
					->addOptions(CSelect::createOptionsFromArray($timezones))
					->setFocusableElementId('label-default-timezone')
					->setAttribute('autofocus', 'autofocus')
			)
			->addRow(new CLabel(_('Default theme'), 'label-default-theme'),
				(new CSelect('default_theme'))
					->setId('default-theme')
					->setFocusableElementId('label-default-theme')
					->setValue($this->getConfig('default_theme'))
					->addOptions(CSelect::createOptionsFromArray(APP::getThemes()))
			);

		return [
			new CTag('h1', true, _('GUI settings')),
			(new CDiv($table))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function stage5(): array {
		$db_type = $this->getConfig('DB_TYPE');
		$databases = CFrontendSetup::getSupportedDatabases();

		$table = new CFormList();
		$table->addRow((new CSpan(_('Database type')))->addClass(ZBX_STYLE_GREY), $databases[$db_type]);

		$db_port = ($this->getConfig('DB_PORT') == 0) ? _('default') : $this->getConfig('DB_PORT');

		if ($this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG) == DB_STORE_CREDS_VAULT) {
			$db_password = _('Stored in HashiCorp Vault secret');
			$db_username = _('Stored in HashiCorp Vault secret');
		}
		else {
			$db_password = preg_replace('/./', '*', $this->getConfig('DB_PASSWORD'));
			$db_username = $this->getConfig('DB_USER');
		}

		$table->addRow((new CSpan(_('Database server')))->addClass(ZBX_STYLE_GREY), $this->getConfig('DB_SERVER'));
		$table->addRow((new CSpan(_('Database port')))->addClass(ZBX_STYLE_GREY), $db_port);
		$table->addRow((new CSpan(_('Database name')))->addClass(ZBX_STYLE_GREY), $this->getConfig('DB_DATABASE'));
		$table->addRow((new CSpan(_('Database user')))->addClass(ZBX_STYLE_GREY), $db_username);
		$table->addRow((new CSpan(_('Database password')))->addClass(ZBX_STYLE_GREY), $db_password);
		if ($db_type === ZBX_DB_POSTGRESQL) {
			$table->addRow((new CSpan(_('Database schema')))->addClass(ZBX_STYLE_GREY), $this->getConfig('DB_SCHEMA'));
		}

		if ($this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG) == DB_STORE_CREDS_VAULT) {
			$table
				->addRow((new CSpan(_('Vault API endpoint')))->addClass(ZBX_STYLE_GREY),
					$this->getConfig('DB_VAULT_URL')
				)
				->addRow((new CSpan(_('Vault secret path')))->addClass(ZBX_STYLE_GREY),
					$this->getConfig('DB_VAULT_DB_PATH')
				)
				->addRow((new CSpan(_('Vault authentication token')))->addClass(ZBX_STYLE_GREY),
					$this->getConfig('DB_VAULT_TOKEN')
				);
		}

		$table->addRow((new CSpan(_('Database TLS encryption')))->addClass(ZBX_STYLE_GREY),
			$this->getConfig('DB_ENCRYPTION') ? 'true' : 'false'
		);
		if ($this->getConfig('DB_ENCRYPTION_ADVANCED')) {
			$table->addRow((new CSpan(_('Database TLS CA file')))->addClass(ZBX_STYLE_GREY),
				$this->getConfig('DB_CA_FILE')
			);
			$table->addRow((new CSpan(_('Database TLS key file')))->addClass(ZBX_STYLE_GREY),
				$this->getConfig('DB_KEY_FILE')
			);
			$table->addRow((new CSpan(_('Database TLS certificate file')))->addClass(ZBX_STYLE_GREY),
				$this->getConfig('DB_CERT_FILE')
			);
			$table->addRow((new CSpan(_('Database host verification')))->addClass(ZBX_STYLE_GREY),
				$this->getConfig('DB_VERIFY_HOST') ? 'true' : 'false'

			);
			if ($db_type === ZBX_DB_MYSQL) {
				$table->addRow((new CSpan(_('Database TLS cipher list')))->addClass(ZBX_STYLE_GREY),
					$this->getConfig('DB_CIPHER_LIST')
				);
			}
		}

		$table->addRow(null, null);

		$table->addRow((new CSpan(_('Zabbix server')))->addClass(ZBX_STYLE_GREY), $this->getConfig('ZBX_SERVER'));
		$table->addRow((new CSpan(_('Zabbix server port')))->addClass(ZBX_STYLE_GREY),
			$this->getConfig('ZBX_SERVER_PORT')
		);
		$table->addRow((new CSpan(_('Zabbix server name')))->addClass(ZBX_STYLE_GREY),
			$this->getConfig('ZBX_SERVER_NAME')
		);

		return [
			new CTag('h1', true, _('Pre-installation summary')),
			(new CDiv([
				new CTag('p', true, _s('Please check configuration parameters. If all is correct, press "%1$s" button, or "%2$s" button to change configuration parameters.', _('Next step'), _('Back'))),
				$table
			]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function stage6(): array {
		$vault_config = [
			'VAULT_URL' => '',
			'VAULT_DB_PATH' => '',
			'VAULT_TOKEN' => ''
		];

		$db_creds_config = [
			'USER' => '',
			'PASSWORD' => ''
		];

		$db_user = null;
		$db_pass = null;
		if ($this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG) == DB_STORE_CREDS_VAULT) {
			$vault_config['VAULT_URL'] = $this->getConfig('DB_VAULT_URL');
			$vault_config['VAULT_DB_PATH'] = $this->getConfig('DB_VAULT_DB_PATH');
			$vault_config['VAULT_TOKEN'] = $this->getConfig('DB_VAULT_TOKEN');

			$vault = new CVaultHelper($vault_config['VAULT_URL'], $vault_config['VAULT_TOKEN']);
			$secret = $vault->loadSecret($vault_config['VAULT_DB_PATH']);

			if (array_key_exists('username', $secret) && array_key_exists('password', $secret)) {
				$db_user = $secret['username'];
				$db_pass = $secret['password'];
			}
			else {
				error(_('Username and password must be stored in Vault secret keys "username" and "password".'));
				$this->STEP_FAILED = true;
				$this->setConfig('step', 2);
				return $this->stage2();
			}
		}
		else {
			$db_creds_config['USER'] = $this->getConfig('DB_USER');
			$db_creds_config['PASSWORD'] = $this->getConfig('DB_PASSWORD');
		}

		$this->dbConnect($db_user, $db_pass);

		$update = [];
		foreach (['default_lang', 'default_timezone', 'default_theme'] as $key) {
			$update[] = $key.'='.zbx_dbstr($this->getConfig($key));
		}
		DBexecute('UPDATE config SET '.implode(',', $update));
		$this->dbClose();

		$this->setConfig('ZBX_CONFIG_FILE_CORRECT', true);

		$config_file_name = APP::getInstance()->getRootDir().CConfigFile::CONFIG_FILE_PATH;
		$config = new CConfigFile($config_file_name);
		$config->config = [
			'DB' => [
				'TYPE' => $this->getConfig('DB_TYPE'),
				'SERVER' => $this->getConfig('DB_SERVER'),
				'PORT' => $this->getConfig('DB_PORT'),
				'DATABASE' => $this->getConfig('DB_DATABASE'),
				'SCHEMA' => $this->getConfig('DB_SCHEMA'),
				'ENCRYPTION' => $this->getConfig('DB_ENCRYPTION'),
				'KEY_FILE' => $this->getConfig('DB_KEY_FILE'),
				'CERT_FILE' => $this->getConfig('DB_CERT_FILE'),
				'CA_FILE' => $this->getConfig('DB_CA_FILE'),
				'VERIFY_HOST' => $this->getConfig('DB_VERIFY_HOST'),
				'CIPHER_LIST' => $this->getConfig('DB_CIPHER_LIST'),
				'DOUBLE_IEEE754' => $this->getConfig('DB_DOUBLE_IEEE754')
			] + $db_creds_config + $vault_config,
			'ZBX_SERVER' => $this->getConfig('ZBX_SERVER'),
			'ZBX_SERVER_PORT' => $this->getConfig('ZBX_SERVER_PORT'),
			'ZBX_SERVER_NAME' => $this->getConfig('ZBX_SERVER_NAME')
		];

		$error = false;

		/*
		 * Create session secret key for first installation. If installation already exists, don't make a new key
		 * because that will terminate the existing session.
		 */
		$db_connect = $this->dbConnect($db_user, $db_pass);
		$is_superadmin = (CWebUser::$data && CWebUser::getType() == USER_TYPE_SUPER_ADMIN);
		$session_key_update_failed = ($db_connect && !$is_superadmin)
			? !CEncryptHelper::updateKey(CEncryptHelper::generateKey())
			: false;

		if (!$db_connect || $session_key_update_failed) {
			$this->STEP_FAILED = true;
			$this->setConfig('step', 2);

			return $this->stage2();
		}

		$this->dbClose();

		if (!$config->save()) {
			$error = true;
			$messages[] = [
				'type' => 'error',
				'message' => $config->error
			];
		}

		if ($error) {
			$this->SHOW_RETRY_BUTTON = true;

			$this->setConfig('ZBX_CONFIG_FILE_CORRECT', false);

			$message_box = makeMessageBox(false, $messages, _('Cannot create the configuration file.'), false, true);
			$message = [
				new CTag('p', true, _('Alternatively, you can install it manually:')),
				new CTag('ol', true, [
					new CTag('li', true, new CLink(_('Download the configuration file'), 'setup.php?save_config=1')),
					new CTag('li', true, _s('Save it as "%1$s"', $config_file_name))
				])
			];
		}
		else {
			$this->DISABLE_CANCEL_BUTTON = true;
			$this->DISABLE_BACK_BUTTON = true;

			// Clear session after success install.
			CSessionHelper::clear();

			$message_box = null;
			$message = [
				(new CTag('h1', true, _('Congratulations! You have successfully installed Zabbix frontend.')))
					->addClass(ZBX_STYLE_GREEN),
				new CTag('p', true, _s('Configuration file "%1$s" created.', $config_file_name))
			];
		}

		return [
			new CTag('h1', true, _('Install')),
			(new CDiv([$message_box, $message]))->addClass(ZBX_STYLE_SETUP_RIGHT_BODY)
		];
	}

	private function dbConnect(?string $username = null, ?string $password = null) {
		global $DB;

		if (!$this->getConfig('check_fields_result')) {
			return false;
		}

		$DB['TYPE'] = $this->getConfig('DB_TYPE');
		if ($DB['TYPE'] === null) {
			return false;
		}

		$DB['SERVER'] = $this->getConfig('DB_SERVER', 'localhost');
		$DB['PORT'] = $this->getConfig('DB_PORT', '0');
		$DB['DATABASE'] = $this->getConfig('DB_DATABASE', 'zabbix');
		$DB['USER'] = $username ? $username : $this->getConfig('DB_USER', 'root');
		$DB['PASSWORD'] = $password ? $password : $this->getConfig('DB_PASSWORD', '');
		$DB['SCHEMA'] = $this->getConfig('DB_SCHEMA', '');
		$DB['ENCRYPTION'] = (bool) $this->getConfig('DB_ENCRYPTION', true);
		$DB['VERIFY_HOST'] = (bool) $this->getConfig('DB_VERIFY_HOST', true);
		$DB['KEY_FILE'] = $this->getConfig('DB_KEY_FILE', '');
		$DB['CERT_FILE'] = $this->getConfig('DB_CERT_FILE', '');
		$DB['CA_FILE'] = $this->getConfig('DB_CA_FILE', '');
		$DB['CIPHER_LIST'] = $this->getConfig('DB_CIPHER_LIST', '');

		$error = '';

		// Check certificate files exists.
		if ($DB['ENCRYPTION'] && ($DB['TYPE'] === ZBX_DB_MYSQL || $DB['TYPE'] === ZBX_DB_POSTGRESQL)) {
			if (($this->getConfig('DB_ENCRYPTION_ADVANCED') || $DB['CA_FILE'] !== '') && !file_exists($DB['CA_FILE'])) {
				return _s('Incorrect file path for "%1$s": %2$s.', _('Database TLS CA file'), $DB['CA_FILE']);
			}

			if ($DB['KEY_FILE'] !== '' && !file_exists($DB['KEY_FILE'])) {
				return _s('Incorrect file path for "%1$s": %2$s.', _('Database TLS key file'), $DB['KEY_FILE']);
			}

			if ($DB['CERT_FILE'] !== '' && !file_exists($DB['CERT_FILE'])) {
				return _s('Incorrect file path for "%1$s": %2$s.', _('Database TLS certificate file'),
					$DB['CERT_FILE']
				);
			}
		}

		// During setup set debug to false to avoid displaying unwanted PHP errors in messages.
		if (DBconnect($error)) {
			return true;
		}
		else {
			return $error;
		}
	}

	private function dbClose(): void {
		global $DB;

		DBclose();

		$DB = null;
	}

	private function checkConnection() {
		global $DB;

		$result = true;

		if (!zbx_empty($DB['SCHEMA']) && $DB['TYPE'] == ZBX_DB_POSTGRESQL) {
			$db_schema = DBselect(
				"SELECT schema_name".
				" FROM information_schema.schemata".
				" WHERE schema_name='".pg_escape_string($DB['SCHEMA'])."'"
			);
			$result = DBfetch($db_schema);
		}

		$db = DB::getDbBackend();

		if (!$db->checkEncoding()) {
			error($db->getWarning());

			return false;
		}

		return $result;
	}

	private function eventHandler(): void {
		if (hasRequest('back') && array_key_exists($this->getStep(), getRequest('back'))) {
			$this->doBack();
		}

		if ($this->getStep() == 1) {
			if (hasRequest('next') && array_key_exists(1, getRequest('next'))) {
				$finalResult = CFrontendSetup::CHECK_OK;
				foreach ($this->frontend_setup->checkRequirements() as $req) {
					if ($req['result'] > $finalResult) {
						$finalResult = $req['result'];
					}
				}

				if ($finalResult == CFrontendSetup::CHECK_FATAL) {
					$this->STEP_FAILED = true;
					unset($_REQUEST['next']);
				}
				else {
					$this->doNext();
				}
			}
		}
		elseif ($this->getStep() == 2) {
			$input = [
				'DB_TYPE' => getRequest('type', $this->getConfig('DB_TYPE')),
				'DB_SERVER' => getRequest('server', $this->getConfig('DB_SERVER', 'localhost')),
				'DB_PORT' => getRequest('port', $this->getConfig('DB_PORT', '0')),
				'DB_DATABASE' => getRequest('database', $this->getConfig('DB_DATABASE', 'zabbix')),
				'DB_USER' => getRequest('user', $this->getConfig('DB_USER', 'root')),
				'DB_PASSWORD' => getRequest('password', $this->getConfig('DB_PASSWORD', '')),
				'DB_SCHEMA' => getRequest('schema', $this->getConfig('DB_SCHEMA', '')),
				'DB_ENCRYPTION' => (bool) getRequest('tls_encryption', $this->getConfig('DB_ENCRYPTION', false)),
				'DB_ENCRYPTION_ADVANCED' => (bool) getRequest('verify_certificate',
					$this->getConfig('DB_ENCRYPTION_ADVANCED', false)
				),
				'DB_VERIFY_HOST' => (bool) getRequest('verify_host', $this->getConfig('DB_VERIFY_HOST', false)),
				'DB_KEY_FILE' => getRequest('key_file', $this->getConfig('DB_KEY_FILE', '')),
				'DB_CERT_FILE' => getRequest('cert_file', $this->getConfig('DB_CERT_FILE', '')),
				'DB_CA_FILE' => getRequest('ca_file', $this->getConfig('DB_CA_FILE', '')),
				'DB_CIPHER_LIST' => getRequest('cipher_list', $this->getConfig('DB_CIPHER_LIST', ''))
			];

			if (!$input['DB_ENCRYPTION_ADVANCED']) {
				$input['DB_KEY_FILE'] = '';
				$input['DB_CERT_FILE'] = '';
				$input['DB_CA_FILE'] = '';
				$input['DB_CIPHER_LIST'] = '';
			}
			else if ($input['DB_TYPE'] === ZBX_DB_MYSQL) {
				$input['DB_VERIFY_HOST'] = true;
			}

			if ($input['DB_TYPE'] !== ZBX_DB_POSTGRESQL) {
				$input['DB_SCHEMA'] = '';
			}

			array_map([$this, 'setConfig'], array_keys($input), $input);

			$creds_storage = getRequest('creds_storage', $this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG));
			$this->setConfig('DB_CREDS_STORAGE', $creds_storage);

			switch ($creds_storage) {
				case DB_STORE_CREDS_CONFIG:
					$this->setConfig('DB_USER', getRequest('user', $this->getConfig('DB_USER', 'root')));
					$this->setConfig('DB_PASSWORD', getRequest('password', $this->getConfig('DB_PASSWORD', '')));
					$this->unsetConfig(['DB_VAULT_URL', 'DB_VAULT_DB_PATH', 'DB_VAULT_TOKEN']);
					break;

				case DB_STORE_CREDS_VAULT:
					$vault_url = getRequest('vault_url', $this->getConfig('DB_VAULT_URL'));
					if (!$vault_url) {
						$vault_url = self::VAULT_URL_DEFAULT;
					}

					$vault_db_path = getRequest('vault_db_path', $this->getConfig('DB_VAULT_DB_PATH'));
					$vault_token = getRequest('vault_token', $this->getConfig('DB_VAULT_TOKEN'));

					$this->setConfig('DB_VAULT_URL', $vault_url);
					$this->setConfig('DB_VAULT_DB_PATH', $vault_db_path);
					$this->setConfig('DB_VAULT_TOKEN', $vault_token);
					$this->unsetConfig(['DB_USER', 'DB_PASSWORD']);
					break;
			}

			if (hasRequest('next') && array_key_exists(2, getRequest('next'))) {
				if ($creds_storage == DB_STORE_CREDS_VAULT) {
					$vault_connection_checked = false;
					$secret_parser = new CVaultSecretParser(['with_key' => false]);
					$secret = [];

					if (ini_get('allow_url_fopen') != 1) {
						error(_('Please enable "allow_url_fopen" directive.'));
					}
					elseif (CVaultHelper::validateVaultApiEndpoint($vault_url)
							&& CVaultHelper::validateVaultToken($vault_token)
							&& $secret_parser->parse($vault_db_path) == CParser::PARSE_SUCCESS) {
						$vault = new CVaultHelper($vault_url, $vault_token);
						$secret = $vault->loadSecret($vault_db_path);

						if ($secret) {
							$vault_connection_checked = true;
						}
					}

					if (!$vault_connection_checked) {
						$db_connected = _('Vault connection failed.');
					}
					elseif (!array_key_exists('username', $secret)
							|| !array_key_exists('password', $secret)) {
						$db_connected = _('Username and password must be stored in Vault secret keys "username" and "password".');
					}
					else {
						$db_connected = $this->dbConnect($secret['username'], $secret['password']);
					}
				}
				else {
					$db_connected = $this->dbConnect();
				}

				if ($db_connected === true) {
					$db_connection_checked = $this->checkConnection();
				}
				else {
					error($db_connected);
					$db_connection_checked = false;
				}

				if ($db_connection_checked) {
					$this->setConfig('DB_DOUBLE_IEEE754', DB::getDbBackend()->isDoubleIEEE754());
				}

				if ($db_connected === true) {
					$this->dbClose();
				}

				if ($db_connection_checked) {
					$this->doNext();
				}
				else {
					$this->STEP_FAILED = true;
					unset($_REQUEST['next']);
				}
			}
		}
		elseif ($this->getStep() == 3) {
			$this->setConfig('ZBX_SERVER', getRequest('zbx_server', $this->getConfig('ZBX_SERVER', 'localhost')));
			$this->setConfig('ZBX_SERVER_PORT', getRequest('zbx_server_port', $this->getConfig('ZBX_SERVER_PORT', '10051')));
			$this->setConfig('ZBX_SERVER_NAME', getRequest('zbx_server_name', $this->getConfig('ZBX_SERVER_NAME', '')));

			if (hasRequest('next') && array_key_exists(3, getRequest('next'))) {
				$this->doNext();
			}
		}
		elseif ($this->getStep() == 6) {
			if (hasRequest('save_config')) {
				$vault_config = [
					'VAULT_URL' => '',
					'VAULT_DB_PATH' => '',
					'VAULT_TOKEN' => ''
				];

				$db_creds_config = [
					'USER' => '',
					'PASSWORD' => ''
				];

				if ($this->getConfig('DB_CREDS_STORAGE', DB_STORE_CREDS_CONFIG) == DB_STORE_CREDS_VAULT) {
					$vault_config['VAULT_URL'] = $this->getConfig('DB_VAULT_URL');
					$vault_config['VAULT_DB_PATH'] = $this->getConfig('DB_VAULT_DB_PATH');
					$vault_config['VAULT_TOKEN'] = $this->getConfig('DB_VAULT_TOKEN');
				}
				else {
					$db_creds_config['USER'] = $this->getConfig('DB_USER');
					$db_creds_config['PASSWORD'] = $this->getConfig('DB_PASSWORD');
				}

				// make zabbix.conf.php downloadable
				header('Content-Type: application/x-httpd-php');
				header('Content-Disposition: attachment; filename="'.basename(CConfigFile::CONFIG_FILE_PATH).'"');
				$config = new CConfigFile(APP::getInstance()->getRootDir().CConfigFile::CONFIG_FILE_PATH);
				$config->config = [
					'DB' => [
						'TYPE' => $this->getConfig('DB_TYPE'),
						'SERVER' => $this->getConfig('DB_SERVER'),
						'PORT' => $this->getConfig('DB_PORT'),
						'DATABASE' => $this->getConfig('DB_DATABASE'),
						'SCHEMA' => $this->getConfig('DB_SCHEMA'),
						'ENCRYPTION' => (bool) $this->getConfig('DB_ENCRYPTION'),
						'VERIFY_HOST' => (bool) $this->getConfig('DB_VERIFY_HOST'),
						'KEY_FILE' => $this->getConfig('DB_KEY_FILE'),
						'CERT_FILE' => $this->getConfig('DB_CERT_FILE'),
						'CA_FILE' => $this->getConfig('DB_CA_FILE'),
						'CIPHER_LIST' => $this->getConfig('DB_CIPHER_LIST'),
						'DOUBLE_IEEE754' => $this->getConfig('DB_DOUBLE_IEEE754')
					] + $db_creds_config + $vault_config,
					'ZBX_SERVER' => $this->getConfig('ZBX_SERVER'),
					'ZBX_SERVER_PORT' => $this->getConfig('ZBX_SERVER_PORT'),
					'ZBX_SERVER_NAME' => $this->getConfig('ZBX_SERVER_NAME')
				];
				die($config->getString());
			}
		}

		if (hasRequest('next') && array_key_exists($this->getStep(), getRequest('next'))) {
			$this->doNext();
		}
	}
}
