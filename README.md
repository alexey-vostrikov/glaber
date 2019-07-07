**[PLEASE READ WIKI](https://gitlab.com/mikler/glaber/wikis/home)**

I am finally releasing the changes I did to make zabbix somewhat faster or let me say, better. Here the most important ones:

    Before you start please make sure that the right reason to start using this code is your will to get some new experience or achieve an extraordinary results. It’s very likely something will break, won’t work and you will be the only one to deal with it (however I will be glad to answer some questions you might have). If you need strong and reliable production system, get a clean vanilla version of zabbix, buy a support.

So, the short list of changes:

    Clickhouse history offloading. Enjoy having data for years without MySQL/Postgress hassle at 50kNVPS.
    Asynchronous SNMP processing. Beware! “Discovery” items will work the old slow synchronous way
    Surprise… Asynchronous agent polling. Enjoy polling all your passive agents in a breeze. A couple of async agent polling threads will do all the work. Ok, ok, maybe 3 or 4 for really big installs (thousands of hosts)
    And a Frankenstein – unreachable poller combines two worlds now – it will try async methods first and after failing them, will use old good sync methods.
    Nmap accessibility checks. IPv4 only. Let me know if you need IPv6, and why.
    Preproc manager with two sockets and queuing control. For those who monitors on really tight hardware.
    Sorry guys, no “fast” widgets yet. They coming. A sort of. I just need to rethink a few points. However for “problems.get” message is working on server. Feel free to use it, and please note that you’ll get only the problems happened since the server start.
    Proxy is not tested yet. We don’t use them anymore. No reason. But sure this is coming also.
    Worker locks are fixed by zabbix team, thank you, Zabbix guys.

First of all, what version ?

The sources is zabbix-4.0.1rc2 The patch will work on both rc1 and rc2 and probably won’t on 4.0.0lts (there are nice cosmetic changes in main THREAD loops which make patch not compartible with it). However, good news: there is no DB upgrade needed, so you can go back/forward without db backup and rollback. Actually you should backup anyway, at least sometimes.
Now, how to put it all together.

First, download the sources:

git clone https://github.com/miklert/zabbix.git

Or the patch and original sources from https://zabbix.com/downloads/.

Unpack, place patch (https://github.com/miklert/zabbix/blob/master/zabbix.patch) inside zabbix-4.0.Xxx folder and patch it:

patch -p1 < the_patch

Then configure, setup, prepare the usual way: https://www.zabbix.com/documentation/4.0/manual/installation/install
And now, the part which is unique for this patched version:
1. Set up asynchronous pollers:

The two fellows are responsible for async SNMP and AGENT collection:

StartPollersAsyncSNMP=10 StartPollersAsyncAGENT=10

You don’t really need many of them. Typically they proccess 600-800 items a second: ./zabbix_server: async snmp poller #23 [got 8192 values in 13.841244 sec, getting values]

Feel free to switch them off by setting =0, so zabbix_server will poll the usual way, using sync processing otherwise they will handle all whatever traffic they can handle.
2. The Clickhouse setup.

I’ve wrote a post someday: https://mmakurov.blogspot.com/2018/07/zabbix-clickhouse-details.html

there is some problems you should know:

    be prepared to have some data delay on graphs which depends on you data rates and clickhouse buffer sizes
    zabbix server starts leaking when it reads str and txt data form history storage. I am trying to find reason for it, but for now fetching str and text values is disabled, but you can still save them and fetch from web ui.
    Latest data panel will not show data dynamics (change in latest metrics to previously connects). You might want to remove the new code and uncomment the "original" version so it will work, but it's too slow for hosts that have more then a hundred items.

3. Nmap:

Zabbix server will use nmap for icmp* checks with packet count set to 1. If you need granular packet loss, say 58%, calculate it in triggers. And setup such an accessibility checks each 10-15 seconds

Now, important note about delays: as items are processed in a bulk way (and also due to my laziness**, they are coming back to queue altogether when all items has been polled. That takes 4-7 seconds in our setup. And a few seconds are needed to processing. So, don’t expect delays to be less then 10 seconds.

However I would be interested to know if you do have such a requirements, perhaps, i’ll have a motivation to optimize it.
4. No new widgets

I really don’t want to release widgets yet as they are still in prototype stage and there is a big architecture problem – what they show is the only true since zabbix restart. I have two alternatives to fix that – either force zabbix_server on start to load all active problems from DB to memory or to ignore DB state on zabbix start and consider all triggers in OK state. Which will break problems start time. And there is really simple fix possible, so I will add separate fixed widgets soon.
5. Proxy compiles, but I haven’t tested it at all.# zabbix

---
**[ПОЖАЛУЙСТА, ПРОЧТИТЕ WIKI](https://gitlab.com/mikler/glaber/wikis/home)**

Я наконецто готов представить изменения, которые сделают Заббикс быстрей или лучше.
Вот основные изменения:
~~~~
Прежде чем мы продолжим, убедитесь, что основная причина использования этого продукта это новый опыт или попытка достичь экстраординарного результата. Скорей всего какой-то функционал будет недоступен, что-то может сломаться и вы должны быть к этому готовы. Если вам необходима стабильная и надежная система, скачайте Заббикс и покупайте их поддержку.
~~~~
Чтож, если вы решили продолжить, вот сам список изменений:
~~~~
Использование Clickhouse для хранения истории. Наслаждайтесь хранением данных годам без Postgres/Mysql при 50k NVPS.
Асинхронные SNMP пуллеры. Внимание! "Обнаружение" итемов работает старым, медленным, синхронным способом.
Сюрприз... Асинхронные пуллеры агентов. Наслаждайтесь опросом всех пассивных агентов вмиг. Несколько поллеров способны сделать всю работу. Окей, окей, возможно потребуется 3-4 для особенно больших систем (тысячи хостов)
И Франкенштейн - пулеры недоступности объединяют два мира - сперва происходит попытка асинхронным методом и, в случае неудачи, переходим к старому синхронному.
Простые проверки с помощью Nmap. Пока только IPv4. Сообщите, если вам потребуется IPv6 и зачем.
Preproc менеджер с двумя сокетами и контролем очередей.
Извините, ребята, пока нет "быстрых" виджетов. Они будут, скорей всего.
Прокси не тестировались. Мы их не используем, поэтому дайте знать, если обнаружите какой-то баг.
~~~~
Теперь о том какая версия?
За основу были взяты исходники zabbix-4.0.8-rc2. Релиз состоится на версии 4.2.
