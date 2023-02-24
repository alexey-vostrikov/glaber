<?php

class CHostStatus extends CTag
{
    public function __construct(int $status, int $maintenance = 0)
    {
        parent::__construct('span', true);

        switch ($status) {
            case HOST_STATUS_MONITORED:
                if ($maintenance == HOST_MAINTENANCE_STATUS_ON) {
                    $this->addItem(_('In maintenance'))->addClass(ZBX_STYLE_ORANGE);
                } else {
                    $this->addItem(_('Enabled'))->addClass(ZBX_STYLE_GREEN);
                }
                break;
            case HOST_STATUS_NOT_MONITORED:
                $this->addItem(_('Disabled'))->addClass(ZBX_STYLE_RED);
                break;
            default:
                $this->addItem(_('Unknown'))->addClass(ZBX_STYLE_GREY);
                break;
        }
    }
}