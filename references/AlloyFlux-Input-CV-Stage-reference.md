# Input CV Stage Reference

Op Amp Reference values for the CV Inputs.

For the MCP6004 and 3.3V output/supply using a -10V reference voltage.

Use a 1n in the feedback loop except for FM where a 100pF is recommended to reduce high frequency noise keeping the audio range clean.

| Input Voltage         | Input Resistor | Reference Resistor | Feedback Resistor | Feedback Capacitor |
| --------------------- | -------------- | ------------------ | ----------------- | ------------------ |
| Bipolar -5/+5V        | 100k           | 91k                | 33k               | 1nF                |
| Bipolar -5/+5V for FM | 100k           | 91k                | 33k               | 100pF              |
| Unipolar 0/10V        | 100k           | 43k                | 33k               | 1nF                |
| VOct 0/+6V (6 oct)    | 62k            | 47k                | 33k               | 1nF                |
| Gate Input 0/5V       | 51k            | 47k                | 33k               | 1nF                |

CV Input voltage ranges:

| CV Input  | Voltage Range  |
| --------- | -------------- |
| V/Oct     | 0/6V           |
| GATE      | 0/5V           |
| REL CV    | -5/+5V         |
| SHAPE CV  | -5/+5V         |
| MOTION CV | -5/+5V         |
| SPACE CV  | -5/+5V         |
| FM IN     | -5/+5V (100pF) |

Since our potentiometers work as attenuators when the CVs are patched, the CV inputs are designed to be bipolar (±5V) to allow for a wider range of modulation possibilities. The V/Oct input is unipolar (0-6V) to accommodate standard pitch control voltages.

Gate input is clamped to 0-5V range, with a threshold around 2.5V for reliable triggering.
