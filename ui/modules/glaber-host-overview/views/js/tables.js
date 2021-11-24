<script type="text/javascript">

$(document).ready( function () {
   //  alert(jQuery.fn.jquery);
   // loadCSS('jquery.dataTables.min.css');
    $('.values-table').DataTable({
        "paging":   false,  
        "info":     true,
        "stateSave": true,
        "dom": 'rtpf',
        "columnDefs": [
            { className: "row-name", "targets": [ 0 ] }
          ]
    });

} );

function loadCSS(filename){ 
    var file = document.createElement("link");
    file.setAttribute("rel", "stylesheet");
    file.setAttribute("type", "text/css");
    file.setAttribute("href", filename);
    document.head.appendChild(file);
 }
</script>
 