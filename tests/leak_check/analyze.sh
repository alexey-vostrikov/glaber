#!/bin/bash
echo "hello"
# https://www.cyberciti.biz/faq/unix-loop-through-files-in-a-directory/
# https://unix.stackexchange.com/questions/37313/how-do-i-grep-for-multiple-patterns-with-pattern-having-a-pipe-character

for file in heaptrack.zabbix_server.*;
 do heaptrack_print $file | grep -E -- 'Debuggee command was|total memory leaked|reading file' | sed ':a;N;$!ba;s/\n/ /g'

 #do echo \n;
 # do echo $file;
 #cat "$file" >> /var/www/cdn.example.com/cache/large.css
done
#for FILE in *; do echo $FILE; done