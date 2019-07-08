**[PLEASE READ WIKI](https://gitlab.com/mikler/glaber/wikis/home)**

I am finally releasing the changes I did to make zabbix somewhat faster or let me say, better. Here the most important ones:

**  Before you start please make sure that the right reason to start using this code is your will to get some new experience or achieve an extraordinary results. It’s very likely something will break, won’t work and you will be the only one to deal with it (however I will be glad to answer some questions you might have). If you need strong and reliable production system, get a clean vanilla version of zabbix, buy a support.**

So, the short list of changes:

    Clickhouse history offloading. Enjoy having data for years without MySQL/Postgress hassle at 50kNVPS.
    Asynchronous SNMP  processing. Beware! “Discovery” items will work the old slow synchronous way
    Surprise… Asynchronous agent polling. Enjoy polling all your passive agents in a breeze. A couple of async agent polling threads will do all the work. Ok, ok, maybe 3 or 4 for really big installs (thousands of hosts)
    And a Frankenstein – unreachable poller combines two worlds now – it will try async methods first and after failing them, will use old good sync methods.
    Nmap accessibility checks. IPv4 only. Let me know if you need IPv6, and why.
    Preproc manager with two sockets and queuing control. For those who monitors on really tight hardware.
    Sorry guys, no “fast” widgets yet. They coming. A sort of. I just need to rethink a few points. However for “problems.get” message is working on server. Feel free to use it, and please note that you’ll get only the problems happened since the server start.
    Proxy is not tested yet. We don’t use them anymore. No reason. But sure this is coming also.
    Worker locks are fixed by zabbix team, thank you, Zabbix guys.

Current release is based on 4.0.8. Release is updates on 2 month cylce basis or of there are real reasons to do so.


Please read WIKI about ways of getting and installing GLaber.
There are also documentation on how to configure certain features
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
