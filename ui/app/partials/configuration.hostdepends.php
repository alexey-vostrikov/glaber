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
                (new CCol(_('Host ID')))->addClass('dep-hostid'),
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

    $hostname_link = (new CLink($hostname, (new CUrl('zabbix.php'))
        ->setArgument('action', 'host.edit')
        ->setArgument('hostid', $hostid)
    ))->addClass(ZBX_STYLE_LINK_ALT);

    return new CRow([
        (new CCol(new CInput('text', "depends[{$dep['depid']}][name]", $dep['name'])))->addClass('dep-name'),
        (new CCol($direction))->addClass('dep-direction'),
        (new CCol(new CInput('text', "depends[{$dep['depid']}][hostid]", $hostid)))->addClass('dep-hostid'),
        (new CCol($hostname_link))->addClass('dep-hostname'),
        (new CCol((new CButton("depends[{$dep['depid']}][remove]", _('Remove')))
            ->addClass(ZBX_STYLE_BTN_LINK)
            ->addClass('element-table-remove')
            ->removeId()
        ))->addClass('dep-action')
    ]);
}


$this->includeJsFile('configuration.hostdepend.js.php', []);
