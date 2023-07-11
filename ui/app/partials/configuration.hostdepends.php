<?php

/**
 * @var CPartial $this
 */

$tab = (new CFormGrid())
    ->setId('host_depends_list')
    ->addItem([
        new CLabel(_('Depends')),
        new CFormField(
            getDependencyTable($data['depends'])
        )
    ]);

$tab->show();

function getDependencyTable(array $depends): CTable
{
    $table = (new CTable())
        ->setId('depends_list')
        ->addClass('depends-list')
        ->setHeader(
            (new CRow([
                (new CCol(_('Name')))->addClass('dep-name'),
                (new CCol(_('Direction')))->addClass('dep-direction'),
                (new CCol(_('Hostname')))->addClass('dep-hostname'),
                (new CCol(_('Actions')))->addClass('dep-action')
            ]))->addClass('depends-list-head')
        );

    foreach ($depends as $dep) {
        $table->addRow(getDependencyRow($dep),'form_row');
    }

    $table->setFooter(
        new CRow([
            (new CCol())->setColSpan(4),
            (new CCol(
                (new CButton("depends_add", _('Add')))
                    ->addClass(ZBX_STYLE_BTN_LINK)
                    ->addClass('element-table-add')
                    ->removeId()
            ))->addClass('dep-action')
        ])
    );

    return $table;
}

function getDependencyRow(array $dep): CRow
{
    $direction = (new CRadioButtonList("depends[{$dep['depid']}][direction]", $dep['direction']))
        ->addValue('Up', _('Up'))
        ->addValue('Down', _('Down'))
        ->setModern();

    if ($dep['direction'] === 'Up') {
        $hostid = $dep['hostid_up'];
        $hostname = $dep['hostname_up'];
    } else {
        $hostid = $dep['hostid_down'];
        $hostname = $dep['hostname_down'];
    }

    $hostname_url = (new CUrl('zabbix.php'))
        ->setArgument('action', 'host.edit')
        ->setArgument('hostid', $hostid);

    $hostname_link = (new CLink($hostname, $hostname_url))
        ->addClass(ZBX_STYLE_LINK_ALT)
        ->setId("depends_{$dep['depid']}_hostlink");

    $hostname_edit = (new CLink(new CAwesomeIcon('pen-to-square'), '#'))
        ->addClass('js-dep-hostname')
        ->setAttribute("data-depid", "{$dep['depid']}");

    return (new CRow([
        (new CCol(new CInput('text', "depends[{$dep['depid']}][name]", $dep['name'])))->addClass('dep-name'),
        (new CCol($direction))->addClass('dep-direction'),
        (new CCol([
            $hostname_link, NBSP(),
            $hostname_edit,
            (new CInput('hidden', "depends[{$dep['depid']}][hostid]", $hostid))->addClass('js-dep-hostid')
        ]))->addClass('dep-hostname'),
        (new CCol((new CButton("depends[{$dep['depid']}][remove]", _('Remove')))
            ->addClass(ZBX_STYLE_BTN_LINK)
            ->addClass('element-table-remove')
            ->removeId()
        ))->addClass('dep-action')
    ]))->addClass('js-form_row');
}


$this->includeJsFile('configuration.hostdepend.js.php', []);
