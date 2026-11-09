# Grandsummoner Intel

**Intel** is a 14 HP modulation and FX companion Eurorack module for [VCV Rack v2](https://vcvrack.com/).

- **Metering**: Master output level ladder or EXT L/R audio processing.
- **Dual FX Engines**: Always-on delay into reverb (with shimmer), knob-paged (DELAY/REVERB toggle buttons switch MIX, TIME, REGEN, and TONE while both engines remain active simultaneously).
- **4 LFO / Trigger Channels**: 4 rate/depth-quantized LFO channels that output rhythmic triggers for drum modules while invisibly modulating adjacent modules.

## Family Adjacency (Connector LED & Expander Link)
- **Universal Adjacency**: Left and right side connector LEDs automatically light up when physically adjacent to any Grandsummoner family module (**Spaces Command**, **Beams**, **Stellar**, or **Explorer**).
- **Multi-Hop Synchronization**: Seamless multi-hop expander messaging with Spaces Command and Beams.

## Building from Source

### Prerequisites
- [VCV Rack SDK v2](https://vcvrack.com/downloads/)
- C++17 compatible compiler toolchain (`make`, `g++`, `mingw-w64`)

### Build Commands
```bash
# Set path to your Rack-SDK directory
export RACK_DIR=/path/to/Rack-SDK

# Build the distributable plugin package
make -j2 dist
```

The resulting package will be placed in `dist/Intel-2.0.0-<platform>.vcvplugin`.

## License
GPL-3.0-or-later. Created by Grandsummoner.
