<?php

class CHostStatus extends CTag
{
    public function __construct(int $status, int $maintenance = 0)
    {
        parent::__construct('glb-hoststatus', false);

        $this->setId(uniqid("glbhs"))
            ->setAttribute('data-status', $status)
            ->setAttribute('data-maintenance', $maintenance);
    }

    public function toString($destroy = true) {
        zbx_add_post_js("const hs=document.getElementById('{$this->getId()}'); 
            try {ReactDOM.createRoot(hs).render(React.createElement(HostStatus, hs.dataset));} 
            catch(err) {console.log(err);}"
        );

        return parent::toString($destroy);
    }
}