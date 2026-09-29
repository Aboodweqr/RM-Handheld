# Direct controller checks (v0.2.7)

The firmware expects individual switches that close to a common ground.
X is GPIO39, Y is GPIO40, LB is GPIO41, RB is GPIO13, LT is GPIO17, and RT
is GPIO18. GPIO42 is unused because its pad was damaged. No other signal
needs moving for this release.

## Both PCBs need ground

A board can have ground without a labelled solder pad. The original ribbon
may have carried ground, power and signals between the halves. Its pinout
has not been verified, so do not assume a particular ribbon pin, metal
mounting hole or copper area is ground.

1. Disconnect all power, including USB and LiPo.
2. If available, reconnect the original ribbon with both boards unpowered.
3. Put one meter probe on the right board's labelled GND. Find a point on
   the left board with a steady resistance close to the meter leads' own
   resistance (near zero ohms). A momentary beep through components is not
   enough.
4. Verify that the switch return contacts connect to that ground. If the
   contacts are scanned matrix lines rather than common-ground switches,
   do not ground them or drive them from ESP GPIO; the PCB needs a different
   interface or isolation.
5. Only after identifying it, connect the left board's ground to ESP GND,
   sharing ground with the right board. Do not power the original controller
   MCU/USB interface while tapping switch lines unless that interface has
   been verified compatible.

Hall sticks also require their correct sensor supply and ground. The sensor
supply voltage and connector pinout must be identified before connecting
power; do not assume that every controller power pad accepts 3.3 V.
ESP32 input signals must remain within 0-3.3 V.

If any left-side button also changes LT and a stick, suspect the missing
common return, an incorrect pad, a bridge, or the original controller
circuit loading the inputs. The symptom does not prove which one.

## Separate GPIO tests from PCB tests

To test X independently of its PCB, first disconnect power and remove its
controller signal wire from GPIO39. Power the ESP over USB, then briefly
connect GPIO39 to ESP GND with a jumper. X should press and release. Do the
same for LB on GPIO41. These are configured input pins, never outputs in
this firmware. Do not jumper a pin to 3V3 or 5V.

On v0.2.7, a GPIO17-to-GND test should give LT, and GPIO18-to-GND should give
RT. Open switches are held released by internal pull-ups. Trigger values
are intentionally 0 or 1023; these switches cannot provide variable travel.

The terminal at 115200 baud prints named events, for example:

    RMH_EVENT 1 X          GPIO39 PRESSED
    RMH_EVENT 2 X          GPIO39 RELEASED
    RMH_EVENT 3 LT         GPIO17 PRESSED
    RMH_EVENT 4 LT         GPIO17 RELEASED

The event number varies. RMH_ADC now lists only the four stick pins
(GPIO1/14/15/16). RMH_ADC_STATUS is a startup plausibility check, not proof
of correct wiring; missing grounds can still produce believable readings.

A successful jumper test with a failed physical-button test points to the
controller wiring/circuit. A serial event without the expected Android
event calls for checking the BLE connection/mapping separately.

## Flash on Bazzite

Close all serial terminals and browser flashers. With esptool already
installed in the environment used for v0.2.6, save the new merged BIN in
Downloads and run:

    sudo setfacl -m "u:$USER:rw" /dev/ttyACM0
    "$HOME/.venvs/rmh-flash/bin/python" -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 115200 write-flash 0x0 "$HOME/Downloads/RM-Handheld-v0.2.7-web-flash.bin"

The port can change after reconnecting. Confirm it with /dev/serial/by-id.
After successful verification, press RESET and leave the sticks untouched
for two seconds. The TFT header says RM27 and the BLE name is RM Handheld 027.
The HID descriptor is unchanged, but if Android retains the old name, forget
the old entry and reconnect. The existing telemetry APK remains compatible.

Build/test success does not confirm the physical ribbon pinout or hardware
operation. Restore the verified common ground before testing the full PCB.
