import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import Backend from 'i18next-http-backend';

i18n
    .use(Backend)
    .use(initReactI18next)
    // init i18next
    // for all options read: https://www.i18next.com/overview/configuration-options
    .init({
        lng: document.getElementsByTagName('html')[0].lang,
        load: 'languageOnly',
        fallbackLng: 'en',
        interpolation: {
            escapeValue: false, // not needed for react as it escapes by default
        },
        backend: {
            loadPath: '/jslocale/{{lng}}/{{ns}}.json',
            allowMultiLoading: false,
        },
    });

export default i18n;