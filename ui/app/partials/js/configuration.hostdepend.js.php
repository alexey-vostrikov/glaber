<script type="text/x-jquery-tmpl" id="dependsRow">
    <tr class="form_row js-form_row">
        <td class="dep-name"><input type="text" name="depends[new#{rowNum}][name]" value=""></td>
        <td class="dep-direction">
            <ul id="depends_new#{rowNum}_direction" class="radio-list-control">
                <li><input type="radio" id="depends_new#{rowNum}_direction_0" name="depends[new#{rowNum}][direction]" value="Up" checked="checked"><label for="depends_new#{rowNum}_direction_0">Up</label></li><li><input type="radio" id="depends_new#{rowNum}_direction_1" name="depends[new#{rowNum}][direction]" value="Down"><label for="depends_new#{rowNum}_direction_1">Down</label></li>
            </ul>
        </td>
        <td class="dep-hostname">
            <a class="link-alt js-dep-hostname" id="depends_new#{rowNum}_hostlink">...</a>&nbsp;<a class="js-dep-hostname" data-depid="new#{rowNum}" href="#"><i class="fa-sharp fa-solid fa-pen-to-square"></i></a>
            <input type="hidden" name="depends[new#{rowNum}][hostid]" class='js-dep-hostid' id="depends_new#{rowNum}_hostid" value="" />
        </td>
        <td class="dep-action"><button type="button" name="depends[new#{rowNum}][remove]" class="btn-link element-table-remove">Remove</button></td>
    </tr>
</script>

<script type="text/javascript">
(() => {
    $('#depends_list').dynamicRows({template: '#dependsRow'});
    $('#depends_list').on('click', '.js-dep-hostname', function (e) {
        e.preventDefault();
        let depid = $(e.target).closest('.js-dep-hostname')[0].dataset.depid;

        return PopUp("popup.generic", {
            'srctbl': 'hosts',
            'srcfld1': 'hostid',
            'reference': 'depends_list#' + depid,
            }, {dialogue_class: "modal-popup-generic"});
    });

    jQuery(document).on('add.popup', function(e, data) {
        parts = data.object.split('#');
        if (parts[0] == 'depends_list') {
            e.preventDefault();
            let idInput = $('#'+parts[0]).find('#depends_'+parts[1]+'_hostid')[0];
            let hostnameLink = $('#'+parts[0]).find('#depends_'+parts[1]+'_hostlink')[0];
            idInput.value = data.values[0].id;
            hostnameLink.text = data.values[0].name;
            hostnameLink.href='zabbix.php?action=host.edit&hostid=' + data.values[0].id;
            console.log([idInput, hostnameLink, data]);
        }
    });
})();
</script>
