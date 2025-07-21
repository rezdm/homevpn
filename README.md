# HomeVPN
Graphical and terminal-based utilities to automate common tasks when connecting to a home VPN.

## Requirements
- GTK
- `curl`
- `ncurses`
- ...~~~~

## Build
Use the provided `build.sh` script (uses CMake internally).

## Usage
1. Copy `config_example` to `~/.homeVPN`.
2. Edit the configuration file to specify the appropriate VPN and mount commands.
3. If required, set the correct ownership and `setuid` permissions on the binary.
