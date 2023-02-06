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


class CDataTable extends CTable {

	protected $message;
	protected $dom = 'Bfrtip';
	protected $rows_per_page = 100;
	//protected $column_defs ='';

	public function __construct($tableid, $options = NULL) {
		parent::__construct();
		if (!$tableid) {
			$tableid = uniqid('t', true);
			$tableid = str_replace('.', '', $tableid);
		}
		
		if (isset($options['compact'])) {
			$this->dom = 'rt';
		}

		$this->includeDataTableCSS();
		$this->rows_per_page = (int) CWebUser::$data['rows_per_page'];
		$this->setId($tableid);
		$this->addClass('display');
		$this->addClass(ZBX_STYLE_LIST_TABLE);
		$this->setNoDataMessage(_('No data found.'));

		$this->addMakeVerticalRotationJs = false;
		
		$this->includeJSFile('js/elements/jquery.dataTables.min.js');
		$this->includeJSFile('js/elements/dataTables.buttons.min.js');
		$this->includeJSFile('js/elements/jszip.min.js');
		$this->includeJSFile('js/elements/buttons.html5.min.js');
		$this->includeJSFile('js/elements/dataTables.searchBuilder.min.js');
		$this->includeTableJS($tableid);
	
	}

	protected function includeJSFile($path) {
		if (!defined('DataTablesLoadedJS'.$path)) {
			?> <script type="text/javascript" src="<?=$path ?>"></script><?php
			define('DataTablesLoadedJS'.$path, true);
		}
	}

	protected function includeTableJS($tableid) {
		?><script type="text/javascript">
			jQuery(document).ready(function() {
			$('#<?=$tableid ?>').DataTable({
			order: [],
			columnDefs: [{ targets  : 'no-sort', orderable: false }, {type: "num", targets: 'type-num'}],

			lengthMenu: [[<?=$this->rows_per_page?>], ['Todos']],
			pageLength: <?=$this->rows_per_page?>,
			buttons: [{extend: 'searchBuilder',
                config: {
                    depthLimit: 2,
					columns: (".search"),

                }}, 'copy', 'csv', 'excel'],
			info:     true,
			stateSave : true,
			dom: '<?=$this->dom ?>',
			stateSave: true,
			});
		});
 		
		</script> <?php
	}

	protected function includeDataTableCSS() {
		?>
		<style><?php require_once dirname(__FILE__).'/css/datatables.css';?> </style>
		<style><?php require_once dirname(__FILE__).'/css/buttons.dataTables.css'; ?></style>
		<style><?php require_once dirname(__FILE__).'/css/searchBuilder.dataTables.css'; ?></style>
		<?php
	}

	public function toString($destroy = true) {
		
		$tableid = $this->getId();
		$string = parent::toString($destroy);

		if ($this->addMakeVerticalRotationJs) {
			$string .= get_js(
				'var makeVerticalRotationForTable = function() {'.
					'jQuery("#'.$tableid.'").makeVerticalRotation();'.
				'}'.
				"\n".
				'if (!jQuery.isReady) {'.
					'jQuery(document).ready(makeVerticalRotationForTable);'.
				'}'.
				'else {'.
					'makeVerticalRotationForTable();'.
				'}',
				true
			);
		}
	
		return $string;
	}

	public function setNoDataMessage($message) {
		$this->message = $message;

		return $this;
	}

	/**
	 * Rotate table header text vertical.
	 * Cells must be marked with "vertical_rotation" class.
	 */
	public function makeVerticalRotation() {
		$this->addMakeVerticalRotationJs = true;

		return $this;
	}

	protected function endToString() {
		$ret = '';
		if ($this->rownum == 0 && $this->message !== null) {
			$ret .= $this->prepareRow(new CCol($this->message), ZBX_STYLE_NOTHING_TO_SHOW)->toString();
		}
		$ret .= parent::endToString();

		return $ret;
	}
}
