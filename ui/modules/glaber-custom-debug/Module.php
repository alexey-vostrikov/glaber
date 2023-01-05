<?php declare(strict_types = 1);
 
namespace Modules\CustomDebug;

use APP;
use CController as CAction;

class Module extends \Zabbix\Core\CModule {
	private $config = [];
	/**
	 * Initialize module.
	 */
	public function init(): void {
	
		APP::Component()->get('menu.main')
			->findOrAdd(_('Administration'))
				->getSubmenu()
					->insertAfter('Queue', 
						(new \CMenuItem(_('Debug')))
							->setAction('module.debug.list'));
					
	
	}
}

?>
