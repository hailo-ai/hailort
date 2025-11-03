#!/bin/bash
set -e

readonly UDEV_RULES_PATH="/etc/udev/rules.d/70-usbnet.rules"
readonly NM_CONNECTION_PATH="/etc/NetworkManager/system-connections/usbnet0.nmconnection"
readonly EXPECTED_IF="usbnet0"
readonly STATIC_IP="192.168.10.11/24"
readonly VENDOR_ID="0525"
readonly PRODUCT_ID="a4a1"

echo "Writing udev rule to $UDEV_RULES_PATH..."
cat <<EOF | sudo tee "$UDEV_RULES_PATH" > /dev/null
SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="$VENDOR_ID", ATTRS{idProduct}=="$PRODUCT_ID", NAME="$EXPECTED_IF"
EOF

echo "Writing NetworkManager config to $NM_CONNECTION_PATH..."
cat <<EOF | sudo tee "$NM_CONNECTION_PATH" > /dev/null
[connection]
id=$EXPECTED_IF
type=ethernet
interface-name=$EXPECTED_IF
autoconnect=true

[ipv4]
method=manual
address1=$STATIC_IP

[ipv6]
method=ignore
EOF

echo "Setting permissions for NetworkManager config..."
sudo chmod 600 "$NM_CONNECTION_PATH"
sudo chown root:root "$NM_CONNECTION_PATH"

echo "Reloading udev rules and triggering..."
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=net
sleep 2

echo "Reloading NetworkManager connections..."
sudo nmcli connection reload

echo "Checking if interface '$EXPECTED_IF' exists..."
if ip link show "$EXPECTED_IF" > /dev/null 2>&1; then
    echo "Interface $EXPECTED_IF detected successfully!"
else
    echo "Interface $EXPECTED_IF NOT detected."
    echo "Please unplug and replug the device or reboot the system."
fi
