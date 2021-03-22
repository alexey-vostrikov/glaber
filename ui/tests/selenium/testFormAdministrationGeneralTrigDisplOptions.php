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

require_once dirname(__FILE__).'/common/testFormAdministrationGeneral.php';

/**
 * @backup config
 */
class testFormAdministrationGeneralTrigDisplOptions extends testFormAdministrationGeneral {

	public $config_link = 'zabbix.php?action=trigdisplay.edit';
	public $form_selector = 'xpath://form[contains(@action, "trigdisplay.update")]';

	public $default = [
		'Use custom event status colours' => false,
		'Unacknowledged PROBLEM events' => true,
		'Acknowledged PROBLEM events' => true,
		'Unacknowledged RESOLVED events' => true,
		'Acknowledged RESOLVED events' => true,
		'id:problem_unack_color' => 'CC0000',
		'id:problem_ack_color'=> 'CC0000',
		'id:ok_unack_color'=> '009900',
		'id:ok_ack_color'=> '009900',
		'Display OK triggers for' => '5m',
		'On status change triggers blink for' => '2m',
		'Not classified' => 'Not classified',
		'Information' => 'Information',
		'Warning' => 'Warning',
		'Average' => 'Average',
		'High' => 'High',
		'Disaster' => 'Disaster',
		'id:severity_color_0' => '97AAB3',
		'id:severity_color_1' => '7499FF',
		'id:severity_color_2' => 'FFC859' ,
		'id:severity_color_3' => 'FFA059',
		'id:severity_color_4' => 'E97659',
		'id:severity_color_5' => 'E45959'
	];

	public $db_default = [
		'custom_color' => 0,
		'problem_unack_style' => 1,
		'problem_ack_style'=> 1,
		'ok_unack_style'=> 1,
		'ok_ack_style'=> 1,
		'problem_unack_color' => 'CC0000',
		'problem_ack_color' => 'CC0000',
		'ok_unack_color' => '009900',
		'ok_ack_color' => '009900',
		'ok_period' => '5m',
		'blink_period' => '2m',
		'severity_name_0' => 'Not classified',
		'severity_name_1' => 'Information',
		'severity_name_2' => 'Warning',
		'severity_name_3' => 'Average',
		'severity_name_4' => 'High',
		'severity_name_5' => 'Disaster',
		'severity_color_0' => '97AAB3',
		'severity_color_1' => '7499FF',
		'severity_color_2' => 'FFC859' ,
		'severity_color_3' => 'FFA059',
		'severity_color_4' => 'E97659',
		'severity_color_5' => 'E45959'
	];

	public $custom = [
		'Use custom event status colours' => true,
		'Unacknowledged PROBLEM events' => false,
		'Acknowledged PROBLEM events' => false,
		'Unacknowledged RESOLVED events' => false,
		'Acknowledged RESOLVED events' => false,
		'id:problem_unack_color' => 'CC0000',
		'id:problem_ack_color'=> 'CC0000',
		'id:ok_unack_color'=> '009900',
		'id:ok_ack_color'=> '009900',
		// This should be changed to really custom values after DEV-1673 is fixed.
//		'id:problem_unack_color' => 'D81B60',
//		'id:problem_ack_color' => 'F8BBD0',
//		'id:ok_unack_color' => '1A237E',
//		'id:ok_ack_color' => 'B3E5FC',
		'Display OK triggers for' => '23h',
		'On status change triggers blink for' => '17h',
		'Not classified' => 'Custom Not classified',
		'Information' => 'Custom Information',
		'Warning' => 'Custom Warning',
		'Average' => 'Custom Average',
		'High' => 'Custom High',
		'High' => 'Custom Disaster',
		'id:severity_color_0' => 'E8EAF6',
		'id:severity_color_1' => 'D1C4E9',
		'id:severity_color_2' => 'B39DDB' ,
		'id:severity_color_3' => '9575CD',
		'id:severity_color_4' => '673AB7',
		'id:severity_color_5' => '4527A0'
	];

	/**
	 * Test for checking form layout.
	 */
	public function testFormAdministrationGeneralTrigDisplOptions_CheckLayout() {
		$this->page->login()->open($this->config_link);
		$this->assertPageTitle('Configuration of trigger displaying options');
		$this->assertPageHeader('Trigger displaying options');
		$form = $this->query($this->form_selector)->waitUntilReady()->asForm()->one();

		$limits = [
			'problem_unack_color' => 6,
			'problem_ack_color' => 6,
			'ok_unack_color' => 6,
			'ok_ack_color' => 6,
			'ok_period' => 32,
			'blink_period' => 32,
			'severity_name_0' => 32,
			'severity_name_1' => 32,
			'severity_name_2' => 32,
			'severity_name_3' => 32,
			'severity_name_4' => 32,
			'severity_name_5' => 32,
			'severity_color_0' => 6,
			'severity_color_1' => 6,
			'severity_color_2' => 6,
			'severity_color_3' => 6,
			'severity_color_4' => 6,
			'severity_color_5' => 6
		];

		foreach ($limits as $id => $limit) {
			$this->assertEquals($limit, $this->query('id', $id)->one()->getAttribute('maxlength'));
		}

		$checkboxes = [
			'custom_color',
			'problem_unack_style',
			'problem_ack_style',
			'ok_unack_style',
			'ok_ack_style'
		];

		foreach ($checkboxes as $checkbox) {
			$this->assertTrue($this->query('id', $checkbox)->one()->isEnabled());
		}

		$colorpickers = [
			'lbl_problem_unack_color',
			'lbl_problem_ack_color',
			'lbl_ok_unack_color',
			'lbl_ok_ack_color',
			'lbl_severity_color_0',
			'lbl_severity_color_1',
			'lbl_severity_color_2',
			'lbl_severity_color_3',
			'lbl_severity_color_4',
			'lbl_severity_color_5'
		];

		$form->fill(['Use custom event status colours' => true]);
		foreach ($colorpickers as $colorpicker) {
			$this->query('id', $colorpicker)->one()->click();
			$overlay = $this->query('id:color_picker')->waitUntilVisible()->asOverlayDialog()->one();
			$overlay->close();
		}

		$event_colors = [
			'problem_unack_color',
			'problem_ack_color',
			'ok_unack_color',
			'ok_ack_color'
		];

		foreach ([true, false] as $status) {
			$form->fill(['Use custom event status colours' => $status]);

			foreach ($event_colors as $input) {
				$this->assertTrue($this->query('id', $input)->one()->isEnabled($status));
			}
		}

		$this->assertEquals(
			'Custom severity names affect all locales and require manual translation!',
			$this->query('class:table-forms-separator')->one()->getText()
		);

		foreach (['Update', 'Reset defaults'] as $button) {
			$this->assertTrue($this->query('button', $button)->one()->isEnabled());
		}
	}

	/**
	 * Test for checking form update without changing any data.
	 */
	public function testFormAdministrationGeneralTrigDisplOptions_SimpleUpdate() {
		$this->executeSimpleUpdate();
	}

	/**
	 * Test for checking 'Reset defaults' button.
	 */
	public function testFormAdministrationGeneralTrigDisplOptions_ResetButton() {
		$this->executeResetButtonTest();
	}

	/**
	 * Test data for Trigger display options form.
	 */
	public function getCheckFormData() {
		return [
			// All valid custom values, checked checkboxes, custom colors.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'Unacknowledged PROBLEM events' => true,
						'Acknowledged PROBLEM events' => true,
						'Unacknowledged RESOLVED events' => true,
						'Acknowledged RESOLVED events' => true,
						'id:problem_unack_color' => 'D81B60',
						'id:problem_ack_color' => 'F8BBD0',
						'id:ok_unack_color' => '1A237E',
						'id:ok_ack_color' => 'B3E5FC',
						'Display OK triggers for' => '25m',
						'On status change triggers blink for' => '12m',
						'Not classified' => 'Test Not classified',
						'Information' => 'Test Information',
						'Warning' => 'Test Warning',
						'Average' => 'Test Average',
						'High' => 'Test High',
						'Disaster' => 'Test Disaster',
						'id:severity_color_0' => 'E8EAF6',
						'id:severity_color_1' => 'D1C4E9',
						'id:severity_color_2' => 'B39DDB' ,
						'id:severity_color_3' => '9575CD',
						'id:severity_color_4' => '673AB7',
						'id:severity_color_5' => '4527A0'
					],
					'db' => [
						'custom_color' => 1,
						'problem_unack_style' => 1,
						'problem_ack_style'=> 1,
						'ok_unack_style'=> 1,
						'ok_ack_style'=> 1,
						'problem_unack_color' => 'D81B60',
						'problem_ack_color' => 'F8BBD0',
						'ok_unack_color' => '1A237E',
						'ok_ack_color' => 'B3E5FC',
						'ok_period' => '25m',
						'blink_period' => '12m',
						'severity_name_0' => 'Test Not classified',
						'severity_name_1' => 'Test Information',
						'severity_name_2' => 'Test Warning',
						'severity_name_3' => 'Test Average',
						'severity_name_4' => 'Test High',
						'severity_name_5' => 'Test Disaster',
						'severity_color_0' => 'E8EAF6',
						'severity_color_1' => 'D1C4E9',
						'severity_color_2' => 'B39DDB' ,
						'severity_color_3' => '9575CD',
						'severity_color_4' => '673AB7',
						'severity_color_5' => '4527A0'
					]
				]
			],
			// Unchecked checkboxes.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Use custom event status colours' => false,
						'Unacknowledged PROBLEM events' => false,
						'Acknowledged PROBLEM events' => false,
						'Unacknowledged RESOLVED events' => false,
						'Acknowledged RESOLVED events' => false
					],
					'db' => [
						'custom_color' => 0,
						'problem_unack_style' => 0,
						'problem_ack_style'=> 0,
						'ok_unack_style'=> 0,
						'ok_ack_style'=> 0
					]
				]
			],
			// Zeros in custom colors.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => '000000',
						'id:problem_ack_color' => '000000',
						'id:ok_unack_color' => '000000',
						'id:ok_ack_color' => '000000',
						'id:severity_color_0' => '000000',
						'id:severity_color_1' => '000000',
						'id:severity_color_2' => '000000' ,
						'id:severity_color_3' => '000000',
						'id:severity_color_4' => '000000',
						'id:severity_color_5' => '000000'
					],
					'db' => [
						'custom_color' => 1,
						'problem_unack_color' => '000000',
						'problem_ack_color' => '000000',
						'ok_unack_color' => '000000',
						'ok_ack_color' => '000000',
						'severity_color_0' => '000000',
						'severity_color_1' => '000000',
						'severity_color_2' => '000000' ,
						'severity_color_3' => '000000',
						'severity_color_4' => '000000',
						'severity_color_5' => '000000'
					]
				]
			],
			// Letters in custom colors.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => 'AAAAAA',
						'id:problem_ack_color' => 'BBBBBB',
						'id:ok_unack_color' => 'CCCCCC',
						'id:ok_ack_color' => 'ABCDEF',
						'id:severity_color_0' => 'AAAAAA',
						'id:severity_color_1' => 'BBBBBB',
						'id:severity_color_2' => 'CCCCCC' ,
						'id:severity_color_3' => 'DDDDDD',
						'id:severity_color_4' => 'EEEEEE',
						'id:severity_color_5' => 'DEDEDE'
					],
					'db' => [
						'custom_color' => 1,
						'problem_unack_color' => 'AAAAAA',
						'problem_ack_color' => 'BBBBBB',
						'ok_unack_color' => 'CCCCCC',
						'ok_ack_color' => 'ABCDEF',
						'severity_color_0' => 'AAAAAA',
						'severity_color_1' => 'BBBBBB',
						'severity_color_2' => 'CCCCCC' ,
						'severity_color_3' => 'DDDDDD',
						'severity_color_4' => 'EEEEEE',
						'severity_color_5' => 'DEDEDE'
					]
				]
			],
			// Maximal valid values.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => '999999',
						'id:problem_ack_color' => '999999',
						'id:ok_unack_color' => '999999',
						'id:ok_ack_color' => '999999',
						'Not classified' => 'NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN',
						'Information' => 'IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII',
						'Warning' => 'WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW',
						'Average' => 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA',
						'High' => 'HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH',
						'Disaster' => 'DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD',
						'id:severity_color_0' => '999999',
						'id:severity_color_1' => '999999',
						'id:severity_color_2' => '999999' ,
						'id:severity_color_3' => '999999',
						'id:severity_color_4' => '999999',
						'id:severity_color_5' => '999999'
					],
					'db' => [
						'custom_color' => 1,
						'problem_unack_color' => '999999',
						'problem_ack_color' => '999999',
						'ok_unack_color' => '999999',
						'ok_ack_color' => '999999',
						'severity_name_0' => 'NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN',
						'severity_name_1' => 'IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII',
						'severity_name_2' => 'WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW',
						'severity_name_3' => 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA',
						'severity_name_4' => 'HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH',
						'severity_name_5' => 'DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD',
						'severity_color_0' => '999999',
						'severity_color_1' => '999999',
						'severity_color_2' => '999999' ,
						'severity_color_3' => '999999',
						'severity_color_4' => '999999',
						'severity_color_5' => '999999'
					]
				]
			],
			// Valid zero values in time in period fields without "s".
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0',
						'On status change triggers blink for' => '0'
					],
					'db' => [
						'ok_period' => '0',
						'blink_period' => '0'
					]
				]
			],
			// Valid zero values in time in period fields with "s".
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0s',
						'On status change triggers blink for' => '0s'
					],
					'db' => [
						'ok_period' => '0s',
						'blink_period' => '0s'
					]
				]
			],
			// Valid zero values in minutes.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0m',
						'On status change triggers blink for' => '0m'
					],
					'db' => [
						'ok_period' => '0m',
						'blink_period' => '0m'
					]
				]
			],
			// Valid zero values in hours.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0h',
						'On status change triggers blink for' => '0h'
					],
					'db' => [
						'ok_period' => '0h',
						'blink_period' => '0h'
					]
				]
			],
			// Valid zero values in days.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0d',
						'On status change triggers blink for' => '0d'
					],
					'db' => [
						'ok_period' => '0d',
						'blink_period' => '0d'
					]
				]
			],
			// Valid zero values in weeks.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '0w',
						'On status change triggers blink for' => '0w'
					],
					'db' => [
						'ok_period' => '0w',
						'blink_period' => '0w'
					]
				]
			],
			// Valid maximum values in period fields in seconds without "s".
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '86400',
						'On status change triggers blink for' => '86400'
					],
					'db' => [
						'ok_period' => '86400',
						'blink_period' => '86400'
					]
				]
			],
			// Valid maximum values in period fields in seconds with "s".
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '86400s',
						'On status change triggers blink for' => '86400s'
					],
					'db' => [
						'ok_period' => '86400s',
						'blink_period' => '86400s'
					]
				]
			],
			// Valid maximum values in period fields with in minutes.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '1440m',
						'On status change triggers blink for' => '1440m'
					],
					'db' => [
						'ok_period' => '1440m',
						'blink_period' => '1440m'
					]
				]
			],
			// Valid maximum values in period fields with in hours.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '24h',
						'On status change triggers blink for' => '24h'
					],
					'db' => [
						'ok_period' => '24h',
						'blink_period' => '24h'
					]
				]
			],
			// Valid maximum values in period fields with in days.
			[
				[
					'expected' => TEST_GOOD,
					'fields' =>  [
						'Display OK triggers for' => '1d',
						'On status change triggers blink for' => '1d'
					],
					'db' => [
						'ok_period' => '1d',
						'blink_period' => '1d'
					]
				]
			],
			// Invalid zero values in Moths (Months not supported).
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '0M',
						'On status change triggers blink for' => '0M',
					],
					'details' => [
						'Incorrect value for field "ok_period": a time unit is expected.',
						'Incorrect value for field "blink_period": a time unit is expected.'
					]
				]
			],
			// Invalid zero values in years (years not supported).
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '0y',
						'On status change triggers blink for' => '0y',
					],
					'details' => [
						'Incorrect value for field "ok_period": a time unit is expected.',
						'Incorrect value for field "blink_period": a time unit is expected.'
					]
				]
			],
			// Invalid maximum values in period fields in seconds without "s".
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '86401',
						'On status change triggers blink for' => '86401',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Invalid maximum values in period fields in seconds with "s".
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '86401s',
						'On status change triggers blink for' => '86401s',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Invalid maximum values in period fields in minutes.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '1441m',
						'On status change triggers blink for' => '1441m',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Invalid maximum values in period fields in hours.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '25h',
						'On status change triggers blink for' => '25h',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Invalid maximum values in period fields in days.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '2d',
						'On status change triggers blink for' => '2d',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Maximal invalid values in period fields.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Display OK triggers for' => '99999999999999999999999999999999',
						'On status change triggers blink for' => '99999999999999999999999999999999',
					],
					'details' => [
						'Incorrect value for field "ok_period": value must be one of 0-86400.',
						'Incorrect value for field "blink_period": value must be one of 0-86400.'
					]
				]
			],
			// Invalid string values.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => 'test',
						'id:problem_ack_color' => 'test',
						'id:ok_unack_color' => 'test',
						'id:ok_ack_color' => 'test',
						'Display OK triggers for' => 'test',
						'On status change triggers blink for' => 'test',
						'id:severity_color_0' => 'test',
						'id:severity_color_1' => 'test',
						'id:severity_color_2' => 'test' ,
						'id:severity_color_3' => 'test',
						'id:severity_color_4' => 'test',
						'id:severity_color_5' => 'test'
					],
					'details' => [
						'Incorrect value for field "problem_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "problem_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_period": a time unit is expected.',
						'Incorrect value for field "blink_period": a time unit is expected.',
						'Incorrect value for field "severity_color_0": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_1": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_2": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_3": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_4": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_5": a hexadecimal colour code (6 symbols) is expected.'
					]
				]
			],
			// Invalid string values.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => '!@#$%^&*()_+',
						'id:problem_ack_color' => '!@#$%^&*()_+',
						'id:ok_unack_color' => '!@#$%^&*()_+',
						'id:ok_ack_color' => '!@#$%^&*()_+',
						'Display OK triggers for' => '!@#$%^&*()_+',
						'On status change triggers blink for' => '!@#$%^&*()_+',
						'id:severity_color_0' => '!@#$%^&*()_+',
						'id:severity_color_1' => '!@#$%^&*()_+',
						'id:severity_color_2' => '!@#$%^&*()_+' ,
						'id:severity_color_3' => '!@#$%^&*()_+',
						'id:severity_color_4' => '!@#$%^&*()_+',
						'id:severity_color_5' => '!@#$%^&*()_+'
					],
					'details' => [
						'Incorrect value for field "problem_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "problem_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_period": a time unit is expected.',
						'Incorrect value for field "blink_period": a time unit is expected.',
						'Incorrect value for field "severity_color_0": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_1": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_2": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_3": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_4": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_5": a hexadecimal colour code (6 symbols) is expected.'
					]
				]
			],
			// Invalid empty values.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => '',
						'id:problem_ack_color'=> '',
						'id:ok_unack_color'=> '',
						'id:ok_ack_color'=> '',
						'Display OK triggers for' => '',
						'On status change triggers blink for' => '',
						'Not classified' => '',
						'Information' => '',
						'Warning' => '',
						'Average' => '',
						'High' => '',
						'Disaster' => '',
						'id:severity_color_0' => '',
						'id:severity_color_1' => '',
						'id:severity_color_2' => '' ,
						'id:severity_color_3' => '',
						'id:severity_color_4' => '',
						'id:severity_color_5' => ''
					],
					'details' => [
						'Incorrect value for field "problem_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "problem_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_period": cannot be empty.',
						'Incorrect value for field "blink_period": cannot be empty.',
						'Incorrect value for field "severity_name_0": cannot be empty.',
						'Incorrect value for field "severity_color_0": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_name_1": cannot be empty.',
						'Incorrect value for field "severity_color_1": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_name_2": cannot be empty.',
						'Incorrect value for field "severity_color_2": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_name_3": cannot be empty.',
						'Incorrect value for field "severity_color_3": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_name_4": cannot be empty.',
						'Incorrect value for field "severity_color_4": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_name_5": cannot be empty.',
						'Incorrect value for field "severity_color_5": a hexadecimal colour code (6 symbols) is expected.'
					]
				]
			],
			// Invalid negative values.
			[
				[
					'expected' => TEST_BAD,
					'fields' =>  [
						'Use custom event status colours' => true,
						'id:problem_unack_color' => '-1',
						'id:problem_ack_color'=> '-1',
						'id:ok_unack_color'=> '-1',
						'id:ok_ack_color'=> '-1',
						'Display OK triggers for' => '-1',
						'On status change triggers blink for' => '-1',
						'id:severity_color_0' => '-1',
						'id:severity_color_1' => '-1',
						'id:severity_color_2' => '-1' ,
						'id:severity_color_3' => '-1',
						'id:severity_color_4' => '-1',
						'id:severity_color_5' => '-1'
					],
					'details' => [
						'Incorrect value for field "problem_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "problem_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_unack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_ack_color": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "ok_period": a time unit is expected.',
						'Incorrect value for field "blink_period": a time unit is expected.',
						'Incorrect value for field "severity_color_0": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_1": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_2": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_3": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_4": a hexadecimal colour code (6 symbols) is expected.',
						'Incorrect value for field "severity_color_5": a hexadecimal colour code (6 symbols) is expected.'
					]
				]
			]
		];
	}

	/**
	 * Backup in needed because of DEV-1673, and can be removed after bug is fixed.
	 * @backup config
	 *
	 * @dataProvider getCheckFormData
	 */
	public function testFormAdministrationGeneralTrigDisplOptions_CheckForm($data) {
		$this->executeCheckForm($data);
	}
}
