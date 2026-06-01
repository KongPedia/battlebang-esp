# `net/`

Wi-Fi connection manager for fleet firmware.

- Wi-Fi credentials come from NVS runtime config after USB provisioning.
- Boot network is forced for fleet operation; manual `start-network` is only a serial recovery/debug command.
- Do not print Wi-Fi passwords in logs.
- MQTT reconnect/resubscribe is triggered when config changes host, port, credentials, root, or turret id.
