# SA-MP Streamer Plugin
[![GitHub Release](https://img.shields.io/github/release/samp-incognito/samp-streamer-plugin.svg)](https://github.com/samp-incognito/samp-streamer-plugin/releases/latest) [![Build Status](https://github.com/samp-incognito/samp-streamer-plugin/actions/workflows/build.yml/badge.svg)](https://github.com/samp-incognito/samp-streamer-plugin/actions/workflows/build.yml)

This plugin streams objects, pickups, checkpoints, race checkpoints, map icons, 3D text labels, and actors at user-defined server ticks. Basic area detection is also included. Because it is written entirely in C++, much of the overhead from PAWN is avoided. This streamer, as a result, is quite a bit faster than any other implementation currently available in PAWN.

## Documentation

Documentation can  be found on the [wiki](https://github.com/samp-incognito/samp-streamer-plugin/wiki).

## Download

The latest binaries for Windows and Linux can be found on the [releases page](https://github.com/samp-incognito/samp-streamer-plugin/releases).

## open.mp Component Support

The plugin now exposes open.mp-compatible component entry points (`ComponentEntryPoint`, `GetComponent`, and `CreateComponent`) so it can be loaded from the `components` directory on open.mp servers. This keeps the existing SA-MP plugin exports intact, so you can continue to load it from the `plugins` folder when running a legacy SA-MP server.
