<?php

class CHostStatus extends CTag
{
    public function __construct(int $status, int $maintenance = 0)
    {
        parent::__construct('glb-host-status', true);
        $this->setAttribute('status', $status)
            ->setAttribute('maintenance', $maintenance);
    }
}