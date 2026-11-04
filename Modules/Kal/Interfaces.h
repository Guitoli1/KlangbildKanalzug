#pragma once

// Maximaler Eingangspegel der Instrumenten-/Hi-Z-Eingänge (0 dBFS bei minimaler Verstärkung),
// laut Herstellerangaben. Gilt nur, wenn der Gain-Regler am Interface auf Minimum steht!
// Quellen: Bedienungsanleitungen bzw. Zusammenstellungen in NAM-Doku und Foren (Stand 2026).

namespace kalibrator
{
struct InterfacePreset { const char* name; float dbu; };

inline const InterfacePreset interfacePresets[] = {
    { "Eigener Wert",                              12.0f },
    { "Focusrite Scarlett 18i20 (4. Gen)",         12.0f },
    { "Focusrite Scarlett 18i20 (3. Gen)",         12.5f },
    { "Focusrite Scarlett (3. Gen)",               12.5f },
    { "Focusrite Scarlett (4. Gen)",               12.0f },
    { "Focusrite Scarlett 2i4 (2. Gen)",           13.0f },
    { "Audient iD4 / iD14 MKII",                   12.0f },
    { "Universal Audio Apollo Twin X (Hi-Z)",      12.2f },
    { "Universal Audio Volt",                      12.5f },
    { "Solid State Logic SSL 2 / 2+",              15.0f },
    { "Arturia MiniFuse",                          11.5f },
    { "Line 6 Helix (als Interface)",              11.5f },
    { "RME Babyface Pro / UCX II (Inst, 0 dB)",    13.0f },
    { "IK Multimedia AXE I/O",                     14.0f },
    { "Zoom Neurig",                               12.4f },
};

inline constexpr int defaultInterfacePreset = 1;   // Scarlett 18i20 (4. Gen)
inline constexpr int numInterfacePresets = (int) (sizeof (interfacePresets) / sizeof (interfacePresets[0]));
} // namespace kalibrator
