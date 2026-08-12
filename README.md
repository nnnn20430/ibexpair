# Ibexpair

Ibexpair is a simple Linux command-line utility for pairing your
second-generation Steam Controller with your puck without needing Steam.

## Usage

Simply place your powered-off Steam Controller on your puck
and run the command. It will automatically pair to the first slot on both.

You can specify the slot used on puck with `-p 1-4` and
the controller with `-c 1-2`.

## Requirements

The program needs access to `hidraw`. The udev rules created by Valve, 
usually provided by distros in `steam-devices` package, should suffice.
Running as root will also work.

## Build

All you need is `make`, `gcc`, `linux-headers` and `libudev`.

Simply run `make`.
