# OmniWipe - Frei0r Transition Plugin for Kdenlive

A frei0r-based, versatile directional wipe transition plugin for Kdenlive. It serves as a flexible tool for wipe transitions with adjustable direction, easing, colored bar, and content-aware optimizations.

<p align="center">
<img width="447" height="463" alt="kdenlive_LQjyiNDgq7" src="https://github.com/user-attachments/assets/e06d946d-d4ee-4763-9731-93e38c239d61" />
</p>

## Features

- **Limit Range to Clips**: Content-aware toggle
    - ON: Detects non-transparent areas in both clips, and restrict the wipe area to its rectangle.
    - OFF: Wipes the entire frame.
- **Direction Axis + Wheel**: Base axis (0°, 90°, 180°) combined with wheel offset for wipes in any angle.
- **Speed Curve (%)**: Controls acceleration/deceleration using power-law easing. Higher values create stronger non-linear timing.
- **Gentle Arrival (%)**: Adds smooth slowdown at the end. It switches between 2 mechanisms depending on the Speed Curve's state.
    - When Speed Curve is 0%: Acts as reverse curve for fast start + deceleration.
    - When Speed Curve is used: Expands a 'deceleration zone' from the clip's end. The wipe will start to slow down as the playback enters this zone. At 100%, the entire clip is under Gentle Arrival's control.
- **Border Type**: None (simple wipe) or Bar (traveling colored bar with width and HSV color controls).
- **Border Blur**: Blurs the border (including the bar).
- **Invert**: Swap incoming and outgoing clips.

## Installation (Windows)

1. Build or obtain the `omni-wipe.dll` and `omni-wipe.xml` files.
2. Place the plugin binary in Kdenlive's frei0r plugins folder (e.g., `kdenlive-master\lib\frei0r-1`).
3. Place the XML file in Kdenlive's transitions folder (e.g., `kdenlive-master\bin\data\kdenlive\transitions`).
4. Restart Kdenlive. The transition appears under Transitions.

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).  
See the LICENSE file for full details.

## Credits

Developed for the open-source video editing community.  
Copyright (C) 2026 acc4commissions and Grok 4.3
