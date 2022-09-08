#!/usr/bin/env bash
set -euo pipefail

SCRIPTS_DIR=$(dirname ${0})
RESOURCES_DIR=$(realpath ${SCRIPTS_DIR}/../resources)

echo "Removing anything under /test"
goliothctl ${GOLIOTHCTL_OPTS} lightdb delete ${GOLIOTH_DEVICE_NAME} test

for file in ${RESOURCES_DIR}/get/*.json; do
    # Workaround for not being able to quickly set different resource paths. Requests always
    # succeed, but unfortunately sometimes data is missing.
    sleep 0.5

    lightdb_path=test/get/$(basename $file .json)
    echo "Setting content of $lightdb_path"
    goliothctl ${GOLIOTHCTL_OPTS} lightdb set ${GOLIOTH_DEVICE_NAME} $lightdb_path -f $file
done
