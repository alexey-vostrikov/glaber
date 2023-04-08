import {useTranslation} from "react-i18next";
import React from "react";

export function HostTemplateLink({name, params}) {
    const { t } = useTranslation();
    const baseurl = 'templates.php';
    if (name && params) {
        let query = (new window.URLSearchParams(params)).toString();
        return <a href={baseurl + '?' + query}>{name}</a>;
    }else{
        return <a href={baseurl}>{t("All templates")}</a>;
    }
}

export function LatestDataLink(props) {
    const { t } = useTranslation();
    const baseurl = 'zabbix.php';
    const query = (new window.URLSearchParams(props)).toString();

    return <a href={baseurl + '?' + query}>{t('Latest data')}</a>;
}

export function HostGraphsLink() {
    return "HostGraphsLink";
}

export function MVCLink({name, action, params}) {
    const query = buildParams({'action':action, ...params});
    return <a href={'zabbix.php?' + query}>{name}</a>;
}

function buildParams(data) {
    const params = new URLSearchParams()

    Object.entries(data).forEach(([key, value]) => {
        if (Array.isArray(value)) {
            value.forEach(value => params.append(key + "[]", value.toString()))
        } else {
            params.append(key, value.toString())
        }
    });

    return params.toString()
}