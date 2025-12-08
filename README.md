# Plosive Remover

A JUCE-based audio plugin for removing plosive sounds (P, B, T pops) from vocal recordings. Available as AU (Logic Pro), VST3, and Standalone formats.

## Features

- **Adaptive threshold detection** - automatically adjusts to input signal level
- **Sensitivity control** (0-24dB) - how easily plosives are detected
- **Reduction control** (0-100%) - how much to attenuate detected plosives
- **Frequency control** (100-400Hz) - cutoff frequency for detection and reduction
- **5ms look-ahead** - catches plosives before they pass through
- **Visual metering** - real-time display of input level, detection, and gain reduction

## Requirements

- macOS (tested on macOS Sequoia)
- Xcode with command line tools
- CMake 3.22+
- Homebrew (for installing CMake if needed)

## Building

```bash
# Clone the repository
git clone https://github.com/mcclowes/plugin-plosive.git
cd plugin-plosive

# Install CMake if needed
brew install cmake

# Clone JUCE framework
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j8
```

Plugins are automatically installed to:
- AU: `~/Library/Audio/Plug-Ins/Components/`
- VST3: `~/Library/Audio/Plug-Ins/VST3/`

## Usage

### In Logic Pro

1. After building, restart Logic Pro (or rescan plugins via Preferences > Plug-in Manager)
2. Add the plugin to an audio track: Audio FX > Audio Units > mcclowes > Plosive Remover
3. Adjust settings to taste

### Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Sensitivity** | 0-24 dB | Higher = more sensitive, triggers on smaller plosives |
| **Reduction** | 0-100% | Amount of gain reduction when plosive detected |
| **Frequency** | 100-400 Hz | Cutoff frequency - higher catches more plosive energy |

### Recommended Starting Settings

- **Sensitivity**: 12 dB
- **Reduction**: 70%
- **Frequency**: 200 Hz

Adjust sensitivity down if too much normal speech is being affected. Adjust frequency up if plosives are still audible.

## How It Works

1. **Detection**: Low-pass filters the input to isolate plosive frequencies (20-300Hz), then tracks the envelope with fast attack/slow release
2. **Adaptive threshold**: Compares current level to a slow-moving average - triggers when level exceeds average by the sensitivity amount
3. **Look-ahead**: 5ms delay buffer allows gain reduction to start before the plosive reaches the output
4. **Reduction**: When triggered, applies gain reduction proportional to how much the signal exceeds the threshold

## Project Structure

```
plugin-plosive/
├── CMakeLists.txt           # Build configuration
├── Source/
│   ├── PluginProcessor.cpp  # DSP and detection logic
│   ├── PluginProcessor.h
│   ├── PluginEditor.cpp     # GUI
│   └── PluginEditor.h
├── JUCE/                    # JUCE framework (git clone, not committed)
└── build/                   # Build artifacts (not committed)
```

## License

MIT

## Acknowledgments

Built with [JUCE](https://juce.com/) framework.
