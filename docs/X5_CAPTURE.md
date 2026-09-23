# Capturing the GameSir X5 Lite report

The USB HID parser works from the controller's own report descriptor. A real
capture is still important because some controller modes use vendor-specific
USB interfaces.

1. Flash the ESP32 firmware and open `idf.py monitor` at 115200 baud.
2. Connect the X5 Lite to the ESP32-S3 native USB host port.
3. Copy the `USB_CONTROLLER` and `USB_DESCRIPTOR` lines.
4. Press each control separately in this order: A, B, X, Y, LB, RB, View, Menu,
   D-pad directions, both sticks, both triggers.
5. Copy the matching `USB_REPORT` lines.

The firmware logs only changed reports and stops after 200 changes, avoiding an
endless serial flood. Do not include unrelated phone or account information in
the capture.

If `USB_CONTROLLER` never appears, the likely problem is USB host wiring/VBUS
or a non-HID XInput interface—not the display or APK. The saved VID/PID and raw
reports allow a small explicit decoder to be added without guessing.
