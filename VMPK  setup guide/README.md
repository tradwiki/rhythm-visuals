# VMPK virtual MIDI setup guide Mac
Setup guide for a virtual MIDI signal on MacOs for DrawPlanck.

1. Install VMPK for Mac
https://vmpk.sourceforge.io/#mac_notes
2. Open the Software
3. **edit > Midi Connections** with the following configurations:
<img src="https://github.com/tradwiki/rhythm-visuals/blob/master/VMPK%20%20setup%20guide/Media/VMPK%20MIDI%20-%20setup%201.png" width="50%" height="50%" />
4. Open Audio MIDI setup from your Mac settings
**Window > Show MIDI studio**
5. Open DrawPlanck in processing and run the code. A list of MIDI devices will appear in the console
6. Change the device value index at line 36 for the value of the VMPK Output (may vary depending on the devices connected to your comptuer)
7. Go back to VMPK **Edit > Keyboard Map** and load vmpk_config.xlm (the file in this repo). The teensy pads are mapped to W, E, S, D keys on your keyboard, the virtual keyboard is mapped to R, T, F, G keys on your keyboard.
*Pay attention to the Base Octaves / Transpose values in VMPK. They should be both at 0.
