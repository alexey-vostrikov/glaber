import { registerTranslateConfig, use } from "lit-translate";
import './components/hoststatus';

registerTranslateConfig({
    loader: lang => fetch(`/jslocale/${lang}.json`).then(res => res.json()),
    // empty: key => key,
});

use(document.getElementsByTagName('html')[0].lang);