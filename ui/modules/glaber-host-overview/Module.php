<?php declare(strict_types = 1);
 
namespace Modules\HostOverview;
 
use APP;
use CController as CAction;
 
class Module extends \Core\CModule {
	/**
	 * Initialize module.
	 */
	public function init(): void {
		// Initialize main menu (CMenu class instance).
		APP::Component()->get('menu.main')
			->findOrAdd(_('Monitoring'))
				->getSubmenu()
					->insertAfter('Hosts', (new \CMenuItem(_('Host overview')))
						->setAction('host.overview')
					);

	}
 
	/**
	 * Event handler, triggered before executing the action.
	 *
	 * @param CAction $action  Action instance responsible for current request.
	 */
	public function onBeforeAction(CAction $action): void {
	}
 
	/**
	 * Event handler, triggered on application exit.
	 *
	 * @param CAction $action  Action instance responsible for current request.
	 */
	public function onTerminate(CAction $action): void {
	}
}
?>
