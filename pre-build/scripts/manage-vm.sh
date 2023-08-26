#!/bin/bash
# Yandex.Cloud API token
# https://cloud.yandex.com/en/docs/iam/operations/iam-token/create

YA_PRIVATE_KEY=$(echo "${YA_PRIVATE_KEY_BASE_64}" | base64 -d)

# Yandex.Cloud API endpoint
API_ENDPOINT_START="https://compute.api.cloud.yandex.net/compute/v1/instances/${YA_VM_ID}:start"
API_ENDPOINT_STOP="https://compute.api.cloud.yandex.net/compute/v1/instances/${YA_VM_ID}:stop"
API_ENDPOINT_GET="https://compute.api.cloud.yandex.net/compute/v1/instances/${YA_VM_ID}"

NOW=$(date +%s)
EXP=$((NOW + 600))

JWT_PAYLOAD="{\"aud\": \"https://iam.api.cloud.yandex.net/iam/v1/tokens\", \"iss\": \"$YA_SERVICE_ACCOUNT_ID\", \"iat\": $NOW, \"exp\": $EXP}"
JWT_HEADER="{\"typ\":\"JWT\",\"alg\":\"PS256\",\"kid\":\"$YA_KEY_ID\"}"

BASE64_HEADER=$(echo -n "$JWT_HEADER" | base64 -w 0)
BASE64_PAYLOAD=$(echo -n "$JWT_PAYLOAD" | base64 -w 0)

HEADER_PAYLOAD="$BASE64_HEADER.$BASE64_PAYLOAD"

SIGNATURE=$(echo -n "$HEADER_PAYLOAD" | openssl dgst -sha256 -sign <(echo -n "$YA_PRIVATE_KEY") -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-1 | base64 -w 0)

JWT="$BASE64_HEADER.$BASE64_PAYLOAD.$SIGNATURE"

API_TOKEN=$(curl -Ss -X POST \
                  -H "Content-Type: application/json" \
                  -d "{\"jwt\":\"$JWT\"}" \
                  https://iam.api.cloud.yandex.net/iam/v1/tokens | \
                  jq -r .iamToken)
# Headers with API token
headers=(
    "Authorization: Bearer ${API_TOKEN}"
    "Content-Type: application/json"
)

check_vm_status() {
    response=$(curl -s -H "${headers[0]}" -H "${headers[1]}" $API_ENDPOINT_GET)
    vm_status=$(echo "$response" | jq -r '.status')
    echo "$vm_status"
}

get_vm_ip() {
    response=$(curl -s -H "${headers[0]}" -H "${headers[1]}" $API_ENDPOINT_GET)
    external_ip=$(echo "$response" | jq -r '.networkInterfaces[].primaryV4Address.oneToOneNat.address')
    echo "$external_ip"
}

start_vm() {
    vm_status=$(check_vm_status)

    if [[ $vm_status == "RUNNING" ]]; then
        echo "VM is already running."
    elif [[ $vm_status == "STOPPED" ]]; then
        response=$(curl -X POST -s -H "${headers[0]}" -H "${headers[1]}" "$API_ENDPOINT_START")

        echo "VM is starting..."

        # Polling loop to check VM status
        while true; do
            vm_status=$(check_vm_status)
            if [[ $vm_status == "RUNNING" ]]; then
                # check ssh connection
                while true; do nc -zv $(get_vm_ip) 22 > /dev/null 2>&1 && break || sleep 10;done
                echo "VM is now running and ready."
                export DOCKER_HOST=ssh://${SSH_USER}@$(get_vm_ip)
                break
            fi

            echo "VM is still starting..."
            sleep 10  # Sleep for 10 seconds before checking again
        done
    else
        echo "Unknown VM status: $vm_status"
    fi
}

stop_vm() {
    vm_status=$(check_vm_status)

    if [[ $vm_status == "STOPPED" ]]; then
        echo "VM is already stopped."
    elif [[ $vm_status == "RUNNING" ]]; then
        response=$(curl -X POST -s -H "${headers[0]}" -H "${headers[1]}" "$API_ENDPOINT_STOP")

        echo "VM is stopping..."

        # Polling loop to check VM status
        while true; do
            vm_status=$(check_vm_status)
            if [[ $vm_status == "STOPPED" ]]; then
                echo "VM has been stopped."
                break
            fi

            echo "VM is still stopping..."
            sleep 10  # Sleep for 10 seconds before checking again
        done
    else
        echo "Unknown VM status: $vm_status"
    fi
}

# Start or stop the VM based on command line argument
if [[ $1 == "start" ]]; then
    start_vm
elif [[ $1 == "stop" ]]; then
    stop_vm
elif [[ $1 == "getip" ]]; then
    get_vm_ip
else
    echo "Usage: $0 [start|stop|getip]"
fi