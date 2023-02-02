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


use Zabbix\Widgets\Fields\CWidgetFieldGraphOverride;

class CWidgetFieldGraphOverrideView extends CWidgetFieldView {

	public function __construct(CWidgetFieldGraphOverride $field) {
		$this->field = $field;
	}

	public function getView(): CList {
		$list = (new CList())->addClass(ZBX_STYLE_OVERRIDES_LIST);

		$i = 0;
		foreach ($this->field->getValue() as $override) {
			$list->addItem($this->getItemTemplate($override, $i));

			$i++;
		}

		$list->addItem(
			(new CDiv(
				(new CButton('override_add', [(new CSpan())->addClass(ZBX_STYLE_PLUS_ICON), _('Add new override')]))
					->addClass(ZBX_STYLE_BTN_ALT)
					->setId('override-add')
			)),
			'overrides-foot'
		);

		return $list;
	}

	public function getJavaScript(): string {
		return '
			// Define it as function to avoid redundancy.
			function initializeOverrides() {
				jQuery("#overrides .'.ZBX_STYLE_OVERRIDES_OPTIONS_LIST.'").overrides({
					add: ".'.ZBX_STYLE_BTN_ALT.'",
					options: "input[type=hidden]",
					captions: '.json_encode($this->getGraphOverrideOptionNames()).',
					makeName: function(option, row_id) {
						return "'.$this->field->getName().'[" + row_id + "][" + option + "]";
					},
					makeOption: function(name) {
						return name.match(
							/.*\[('.implode('|', $this->field->getOverrideOptions()).')\]/
						)[1];
					},
					override: ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'",
					overridesList: ".'.ZBX_STYLE_OVERRIDES_LIST.'",
					onUpdate: () => widget_svggraph_form.onGraphConfigChange(),
					menu: '.json_encode($this->getGraphOverrideMenu()).'
				});
			}

			// Initialize dynamicRows.
			jQuery("#overrides")
				.dynamicRows({
					template: "#overrides-row",
					beforeRow: ".overrides-foot",
					remove: ".'.ZBX_STYLE_BTN_REMOVE.'",
					add: "#override-add",
					row: ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'"
				})
				.bind("afteradd.dynamicRows", function(event, options) {
					const container = jQuery(".overlay-dialogue-body");

					container.scrollTop(Math.max(container.scrollTop(),
						jQuery("#widget-dialogue-form")[0].scrollHeight - container.height()
					));

					jQuery(".multiselect", jQuery("#overrides")).each(function() {
						jQuery(this).multiSelect(jQuery(this).data("params"));
					});

					widget_svggraph_form.updateVariableOrder(jQuery("#overrides"), ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'", "or");
					widget_svggraph_form.onGraphConfigChange();
				})
				.bind("afterremove.dynamicRows", function(event, options) {
					widget_svggraph_form.updateVariableOrder(jQuery("#overrides"), ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'", "or");
					widget_svggraph_form.onGraphConfigChange();
				})
				.bind("tableupdate.dynamicRows", function(event, options) {
					widget_svggraph_form.updateVariableOrder(jQuery("#overrides"), ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'", "or");
					initializeOverrides();
					if (jQuery("#overrides .'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'").length > 1) {
						jQuery("#overrides .drag-icon").removeClass("disabled");
						jQuery("#overrides").sortable("enable");
					}
					else {
						jQuery("#overrides .drag-icon").addClass("disabled");
						jQuery("#overrides").sortable("disable");
					}
				});

			// Initialize overrides UI control.
			initializeOverrides();

			// Initialize override pattern-selectors.
			jQuery(".multiselect", jQuery("#overrides")).each(function() {
				jQuery(this).multiSelect(jQuery(this).data("params"));
			});

			// Make overrides sortable.
			if (jQuery("#overrides .'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'").length < 2) {
				jQuery("#overrides .drag-icon").addClass("disabled");
			}

			jQuery("#overrides").sortable({
				items: ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'",
				containment: "parent",
				handle: ".drag-icon",
				tolerance: "pointer",
				scroll: false,
				cursor: "grabbing",
				opacity: 0.6,
				axis: "y",
				disabled: function() {
					return jQuery("#overrides .'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'").length < 2;
				}(),
				start: function() { // Workaround to fix wrong scrolling at initial sort.
					jQuery(this).sortable("refreshPositions");
				},
				stop: () => widget_svggraph_form.onGraphConfigChange(),
				update: function() {
					widget_svggraph_form.updateVariableOrder(jQuery("#overrides"), ".'.ZBX_STYLE_OVERRIDES_LIST_ITEM.'", "or");
				}
			});
		';
	}

	public function getTemplates(): array {
		return [
			new CTemplateTag('overrides-row', $this->getItemTemplate(CWidgetFieldGraphOverride::getDefaults()))
		];
	}

	/**
	 * Function returns array containing string values used as titles for override options.
	 */
	private function getGraphOverrideOptionNames(): array {
		return [
			'width' => _('Width'),
			'type' => _('Draw'),
			'type'.SVG_GRAPH_TYPE_LINE => _('Line'),
			'type'.SVG_GRAPH_TYPE_POINTS => _('Points'),
			'type'.SVG_GRAPH_TYPE_STAIRCASE => _('Staircase'),
			'type'.SVG_GRAPH_TYPE_BAR => _('Bar'),
			'transparency' => _('Transparency'),
			'fill' => _('Fill'),
			'pointsize' => _('Point size'),
			'missingdatafunc' => _('Missing data'),
			'missingdatafunc'.SVG_GRAPH_MISSING_DATA_NONE => _('None'),
			'missingdatafunc'.SVG_GRAPH_MISSING_DATA_CONNECTED => _x('Connected', 'missing data function'),
			'missingdatafunc'.SVG_GRAPH_MISSING_DATA_TREAT_AS_ZERO => _x('Treat as 0', 'missing data function'),
			'missingdatafunc'.SVG_GRAPH_MISSING_DATA_LAST_KNOWN => _x('Last known', 'missing data function'),
			'axisy' => _('Y-axis'),
			'axisy'.GRAPH_YAXIS_SIDE_LEFT => _('Left'),
			'axisy'.GRAPH_YAXIS_SIDE_RIGHT => _('Right'),
			'timeshift' => _('Time shift')
		];
	}

	/**
	 * Function returns array used to construct override field menu of available override options.
	 */
	private function getGraphOverrideMenu(): array {
		return [
			'sections' => [
				[
					'name' => _('ADD OVERRIDE'),
					'options' => [
						['name' => _('Base color'), 'callback' => 'addOverride', 'args' => ['color', '']],

						['name' => _('Width').'/0', 'callback' => 'addOverride', 'args' => ['width', 0]],
						['name' => _('Width').'/1', 'callback' => 'addOverride', 'args' => ['width', 1]],
						['name' => _('Width').'/2', 'callback' => 'addOverride', 'args' => ['width', 2]],
						['name' => _('Width').'/3', 'callback' => 'addOverride', 'args' => ['width', 3]],
						['name' => _('Width').'/4', 'callback' => 'addOverride', 'args' => ['width', 4]],
						['name' => _('Width').'/5', 'callback' => 'addOverride', 'args' => ['width', 5]],
						['name' => _('Width').'/6', 'callback' => 'addOverride', 'args' => ['width', 6]],
						['name' => _('Width').'/7', 'callback' => 'addOverride', 'args' => ['width', 7]],
						['name' => _('Width').'/8', 'callback' => 'addOverride', 'args' => ['width', 8]],
						['name' => _('Width').'/9', 'callback' => 'addOverride', 'args' => ['width', 9]],
						['name' => _('Width').'/10', 'callback' => 'addOverride', 'args' => ['width', 10]],

						['name' => _('Draw').'/'._('Line'), 'callback' => 'addOverride', 'args' => ['type', SVG_GRAPH_TYPE_LINE]],
						['name' => _('Draw').'/'._('Points'), 'callback' => 'addOverride', 'args' => ['type', SVG_GRAPH_TYPE_POINTS]],
						['name' => _('Draw').'/'._('Staircase'), 'callback' => 'addOverride', 'args' => ['type', SVG_GRAPH_TYPE_STAIRCASE]],
						['name' => _('Draw').'/'._('Bar'), 'callback' => 'addOverride', 'args' => ['type', SVG_GRAPH_TYPE_BAR]],

						['name' => _('Transparency').'/0', 'callback' => 'addOverride', 'args' => ['transparency', 0]],
						['name' => _('Transparency').'/1', 'callback' => 'addOverride', 'args' => ['transparency', 1]],
						['name' => _('Transparency').'/2', 'callback' => 'addOverride', 'args' => ['transparency', 2]],
						['name' => _('Transparency').'/3', 'callback' => 'addOverride', 'args' => ['transparency', 3]],
						['name' => _('Transparency').'/4', 'callback' => 'addOverride', 'args' => ['transparency', 4]],
						['name' => _('Transparency').'/5', 'callback' => 'addOverride', 'args' => ['transparency', 5]],
						['name' => _('Transparency').'/6', 'callback' => 'addOverride', 'args' => ['transparency', 6]],
						['name' => _('Transparency').'/7', 'callback' => 'addOverride', 'args' => ['transparency', 7]],
						['name' => _('Transparency').'/8', 'callback' => 'addOverride', 'args' => ['transparency', 8]],
						['name' => _('Transparency').'/9', 'callback' => 'addOverride', 'args' => ['transparency', 9]],
						['name' => _('Transparency').'/10', 'callback' => 'addOverride', 'args' => ['transparency', 10]],

						['name' => _('Fill').'/0', 'callback' => 'addOverride', 'args' => ['fill', 0]],
						['name' => _('Fill').'/1', 'callback' => 'addOverride', 'args' => ['fill', 1]],
						['name' => _('Fill').'/2', 'callback' => 'addOverride', 'args' => ['fill', 2]],
						['name' => _('Fill').'/3', 'callback' => 'addOverride', 'args' => ['fill', 3]],
						['name' => _('Fill').'/4', 'callback' => 'addOverride', 'args' => ['fill', 4]],
						['name' => _('Fill').'/5', 'callback' => 'addOverride', 'args' => ['fill', 5]],
						['name' => _('Fill').'/6', 'callback' => 'addOverride', 'args' => ['fill', 6]],
						['name' => _('Fill').'/7', 'callback' => 'addOverride', 'args' => ['fill', 7]],
						['name' => _('Fill').'/8', 'callback' => 'addOverride', 'args' => ['fill', 8]],
						['name' => _('Fill').'/9', 'callback' => 'addOverride', 'args' => ['fill', 9]],
						['name' => _('Fill').'/10', 'callback' => 'addOverride', 'args' => ['fill', 10]],

						['name' => _('Point size').'/1', 'callback' => 'addOverride', 'args' => ['pointsize', 1]],
						['name' => _('Point size').'/2', 'callback' => 'addOverride', 'args' => ['pointsize', 2]],
						['name' => _('Point size').'/3', 'callback' => 'addOverride', 'args' => ['pointsize', 3]],
						['name' => _('Point size').'/4', 'callback' => 'addOverride', 'args' => ['pointsize', 4]],
						['name' => _('Point size').'/5', 'callback' => 'addOverride', 'args' => ['pointsize', 5]],
						['name' => _('Point size').'/6', 'callback' => 'addOverride', 'args' => ['pointsize', 6]],
						['name' => _('Point size').'/7', 'callback' => 'addOverride', 'args' => ['pointsize', 7]],
						['name' => _('Point size').'/8', 'callback' => 'addOverride', 'args' => ['pointsize', 8]],
						['name' => _('Point size').'/9', 'callback' => 'addOverride', 'args' => ['pointsize', 9]],
						['name' => _('Point size').'/10', 'callback' => 'addOverride', 'args' => ['pointsize', 10]],

						['name' => _('Missing data').'/'._('None'), 'callback' => 'addOverride', 'args' => ['missingdatafunc', SVG_GRAPH_MISSING_DATA_NONE]],
						['name' => _('Missing data').'/'._x('Connected', 'missing data function'), 'callback' => 'addOverride', 'args' => ['missingdatafunc', SVG_GRAPH_MISSING_DATA_CONNECTED]],
						['name' => _('Missing data').'/'._x('Treat as 0', 'missing data function'), 'callback' => 'addOverride', 'args' => ['missingdatafunc', SVG_GRAPH_MISSING_DATA_TREAT_AS_ZERO]],
						['name' => _('Missing data').'/'._x('Last known', 'missing data function'), 'callback' => 'addOverride', 'args' => ['missingdatafunc', SVG_GRAPH_MISSING_DATA_LAST_KNOWN]],

						['name' => _('Y-axis').'/'._('Left'), 'callback' => 'addOverride', 'args' => ['axisy', GRAPH_YAXIS_SIDE_LEFT]],
						['name' => _('Y-axis').'/'._('Right'), 'callback' => 'addOverride', 'args' => ['axisy', GRAPH_YAXIS_SIDE_RIGHT]],

						['name' => _('Time shift'), 'callback' => 'addOverride', 'args' => ['timeshift']]
					]
				]
			]
		];
	}

	private function getItemTemplate(array $value, $row_num = '#{rowNum}'): CListItem {
		$inputs = [];

		// Create override options list.
		foreach ($this->field->getOverrideOptions() as $option) {
			if (array_key_exists($option, $value)) {
				$inputs[] = (new CVar($this->field->getName().'['.$row_num.']['.$option.']', $value[$option]));
			}
		}

		$host_pattern_field = (new CPatternSelect([
			'name' => $this->field->getName().'['.$row_num.'][hosts][]',
			'object_name' => 'hosts',
			'data' => $value['hosts'],
			'placeholder' => _('host pattern'),
			'wildcard_allowed' => 1,
			'popup' => [
				'parameters' => [
					'srctbl' => 'hosts',
					'srcfld1' => 'hostid',
					'dstfrm' => $this->form_name,
					'dstfld1' => zbx_formatDomId($this->field->getName().'['.$row_num.'][hosts][]')
				]
			],
			'add_post_js' => false
		]))
			->setEnabled(!$this->isDisabled())
			->setAriaRequired($this->isRequired());

		return (new CListItem([
			(new CDiv())->addClass(ZBX_STYLE_DRAG_ICON),
			$host_pattern_field,
			(new CPatternSelect([
				'name' => $this->field->getName().'['.$row_num.'][items][]',
				'object_name' => 'items',
				'data' => $value['items'],
				'placeholder' => _('item pattern'),
				'wildcard_allowed' => 1,
				'popup' => [
					'parameters' => [
						'srctbl' => 'items',
						'srcfld1' => 'itemid',
						'real_hosts' => 1,
						'numeric' => 1,
						'dstfrm' => $this->form_name,
						'dstfld1' => zbx_formatDomId($this->field->getName().'['.$row_num.'][items][]')
					],
					'filter_preselect' => [
						'id' => $host_pattern_field->getId(),
						'submit_as' => 'host_pattern',
						'submit_parameters' => [
							'host_pattern_wildcard_allowed' => 1,
							'host_pattern_multiple' => 1
						],
						'multiple' => true
					]
				],
				'autosuggest' => [
					'filter_preselect' => [
						'id' => $host_pattern_field->getId(),
						'submit_as' => 'host_pattern',
						'submit_parameters' => [
							'host_pattern_wildcard_allowed' => 1,
							'host_pattern_multiple' => 1
						],
						'multiple' => true
					]
				],
				'add_post_js' => false
			]))
				->setEnabled(!$this->isDisabled())
				->setAriaRequired($this->isRequired()),
			(new CDiv(
				(new CButton())
					->setAttribute('title', _('Delete'))
					->addClass(ZBX_STYLE_BTN_REMOVE)
					->removeId()
			))->addClass('dataset-actions'),
			(new CList($inputs))
				->addClass(ZBX_STYLE_OVERRIDES_OPTIONS_LIST)
				->addItem(
					(new CButton(null, (new CSpan())->addClass(ZBX_STYLE_PLUS_ICON)))
						->setAttribute('data-row', $row_num)
						->addClass(ZBX_STYLE_BTN_ALT)
				)
		]))->addClass(ZBX_STYLE_OVERRIDES_LIST_ITEM);
	}
}
