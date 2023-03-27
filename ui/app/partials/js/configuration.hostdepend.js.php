<script type="text/x-jquery-tmpl" id="dependsRow">
    <tr class="form_row">
        <td class="dep-name"><input type="text" name="depends[new#{rowNum}][name]" value=""></td>
        <td class="dep-direction">
            <ul id="depends_new#{rowNum}_direction" class="radio-list-control">
                <li><input type="radio" id="depends_new#{rowNum}_direction_0" name="depends[new#{rowNum}][direction]" value="Up" checked="checked"><label for="depends_new#{rowNum}_direction_0">Up</label></li><li><input type="radio" id="depends_new#{rowNum}_direction_1" name="depends[new#{rowNum}][direction]" value="Down"><label for="depends_new#{rowNum}_direction_1">Down</label></li>
            </ul>
        </td>
        <td class="dep-hostid"><input type="text" name="depends[new#{rowNum}][hostid]" value="" /></td>
        <td class="dep-hostname">...</td>
        <td class="dep-action"><button type="button" name="depends[new#{rowNum}][remove]" class="btn-link element-table-remove">Remove</button></td>
    </tr>
</script>

<script type="text/javascript">
(() => {
    $('#depends_list').dynamicRows({template: '#dependsRow'});
})();
</script>
<script type="text/javascript">
</script>
