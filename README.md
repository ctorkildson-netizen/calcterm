# calcterm

A VT100 terminal for the PicoCalc (RP2350, Pico 2 W). For now it talks over a serial line; telnet, SSH and an FTP client are to follow.

The screen is 64 columns by 32 rows, in Blair Leduc's 5×10 font with the VT100's line-drawing characters added, so boxes join up.

## What it does

- **VT100**, after the VT100 User Guide: cursor movement, erasing, scrolling regions, origin mode, tab stops, insert and delete of characters and lines, double-height lines excepted. It answers the host's questions (DA, DSR, the cursor-position report).
- **VT52 mode**, entered and left as the VT100 does (`ESC [ ? 2 l`, `ESC <`).
- **What Linux sends anyway**: the 16 colours (256-colour and RGB requests are matched to the nearest of them), bold, underline, reverse, the alternate screen that `vim`, `less` and `htop` use, and UTF-8. Box-drawing characters arrive as UTF-8 from most programs today and are shown with the line-drawing glyphs; other non-ASCII characters show as `?`.
- **Anything it doesn't know is ignored**, window titles included, so it never leaves litter on the screen.

## Keys

| Key | Sends |
| --- | --- |
| F1–F10 | function keys: F1–F4 as the VT100's PF1–PF4, F5–F10 as xterm's (Shift+F1–F5 gives F6–F10) |
| Arrows | cursor keys (application mode when the host asks) |
| Home, End, Ins, Del, PgUp, PgDn | the xterm sequences |
| Backspace | DEL (`^?`), or BS — see the setup menu |
| Alt + key | Esc then the key (meta) |
| Ctrl tapped on its own | makes the next key a Ctrl key (a `^` shows in the top-right corner) |
| Brk | a quarter-second break on the line |
| Alt+F4 | a screenshot, to `/screenshots` on the card (a .bmp, and a .txt of the screen's text) |
| **Alt+F5** | **the setup menu** |

## The setup menu (Alt+F5)

Port, speed, format (8N1, 7E1 and so on), XON/XOFF, local echo, what Enter sends, and what Backspace sends. Each line has a highlighted letter that changes or does it in one key (P for the port, S the speed, T a screenshot, Q to quit calcterm for the PicoCalc's own shell, and so on); the arrow keys work too. Changes take effect at once. *Save these settings* writes them to `/calcterm/calcterm.cfg` on the card (a folder of its own, as the PicoCalc's programs keep theirs), a plain text file that can be edited by hand. The menu also sends a break, resets the terminal, and quits calcterm for the PicoCalc's shell (the host's session carries on, to be picked up next time).

## The serial ports

- **USB-C (UART0, the default).** The PicoCalc's USB-C socket goes to the Pico's UART0 (GP0/GP1) through its CH340, so a USB cable to another computer is a serial line with nothing else needed.
- **GP4/GP5 (UART1).** For a real RS-232 port, through a MAX3232 level-shifter board: GP4 is TX, GP5 RX. Don't use the UART0 pins on the side header for this; the CH340 is on the same lines.

### Wiring a MAX3232 board

The pins are the upper of the two rows of eight holes on the PicoCalc's left side (the "Core GPIOs" in ClockworkPi's assembly guide). Counting from the end nearest the volume knob:

| Hole | Pin | MAX3232 board |
| --- | --- | --- |
| 1 | 3V3 OUT | VCC (3.3 V, not 5 V) |
| 4 | GP4 (TX) | see below |
| 5 | GP5 (RX) | see below |
| 8 | GND | GND |

Boards differ in how they label TX and RX. Most name the direction of the signal (connect the board's RXD to the Pico's TX). Some, such as the common blue HW-044, name the pin they connect to: on those, TXD goes to GP4 and RXD to GP5. If nothing comes through, swap the two wires; the wrong way round does no harm. Don't use the lower row's UART1 holes: those go to the keyboard's own processor, not the Pico.

To check the PicoCalc's half on its own, bridge pins 2 and 3 of the board's DB9 socket: with local echo off, what you type should come back. From the board's DB9 to a PC is a null-modem connection (both ends are terminals).

XON/XOFF is on by default: if the PicoCalc falls behind (it has a 16 KB buffer), it asks the host to pause.

## Trying it with Linux

Plug the PicoCalc's USB-C into the Linux machine. The CH340 shows up as `/dev/ttyUSB0` (`dmesg | tail` says for certain). On Ubuntu and its relatives, if the port vanishes a moment after it appears, the braille-display service has taken it: `sudo apt remove brltty`.

A quick test, typing at each end:

    picocom -b 115200 /dev/ttyUSB0          # Ctrl-A Ctrl-X to leave

### The terminfo entry

`calcterm.ti` tells Linux programs exactly what calcterm can do: its size, colours, line drawing and keys. Copy it to the host and install it once:

    sudo tic -x calcterm.ti

Then `TERM=calcterm` gives colour, line drawing and working function keys in `htop`, `mc`, `vim` and the rest, with nothing borrowed from vt100 or xterm.

### A login on the PicoCalc

The PicoCalc doesn't drive the line's carrier-detect signal, so agetty has to be told it's a local line (`-L`), or it waits for a carrier that never comes. Once, to set that up:

    sudo systemctl edit serial-getty@ttyUSB0.service

and put in

    [Service]
    ExecStart=
    ExecStart=-/sbin/agetty -L -o '-p -- \\u' 115200 %I calcterm

(the empty `ExecStart=` clears the original; use `vt100` in place of `calcterm` if the entry isn't installed). Then

    sudo systemctl restart serial-getty@ttyUSB0.service

and press Enter on the PicoCalc for the login prompt. After logging in, type this on the PicoCalc (it sets the size of the PicoCalc's own line; typed in a terminal on the Linux machine's screen it changes that one instead):

    stty rows 32 cols 64

To have it done at every login on that line, add this to `~/.bashrc` on the Linux machine:

    [ "$(tty)" = /dev/ttyUSB0 ] && stty rows 32 cols 64

If Midnight Commander's top line scrolls off the screen, or its key bar wraps, this is what's missing.

(`resize`, from the xterm package, finds the size by asking the terminal.) If the Up arrow moves the cursor rather than recalling the last command, the login shell is plain `sh`: `chsh -s /bin/bash`. `sudo systemctl stop serial-getty@ttyUSB0.service` ends it.

### Midnight Commander

Typing long shell commands on the PicoCalc's keyboard is a chore. [Midnight Commander](https://midnight-commander.org/) (`mc`) is a full-screen file manager in the style of Norton Commander: two directory panels, the arrow keys to move, Enter to open or run, and the function keys along the bottom to view, edit, copy, move, make folders and delete. It fits calcterm well, and makes the PicoCalc a comfortable way to look after another machine:

    sudo apt install mc
    mc

Its command line is still there at the bottom for when you need it, and F10 leaves it.

### Other tests

`vim`, `less`, `htop`, `top` and `vttest 32x64` are good ones.

## Building

With the Pico SDK (2.2.0) and `arm-none-eabi-gcc`:

    git clone --recurse-submodules https://github.com/ctorkildson-netizen/calcterm
    cd calcterm
    cmake -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
    ninja -C build                 # build/calcterm.uf2

Copy `calcterm.uf2` to the card's `pico2-apps` folder and start it from UF2 Loader or the shell.

## Tests

`host/` has the terminal's tests, which run on Linux: `make -C host check`. `test_vt` checks the escape sequences one by one. `ptyrun` runs a real program against the terminal and prints the screen it leaves. `screentest` does the same through the renderer, against a model of the LCD and its scrolling, and checks the result matches a fresh whole-screen draw pixel for pixel.

## Built on

- [picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter) (Blair Leduc): the LCD, keyboard and SD card drivers, and the font.
- [picocalc-shell](https://github.com/BlairLeduc/picocalc-shell) (Blair Leduc): returning to the shell.
- [picocalc-extras](https://github.com/ctorkildson-netizen/picocalc-extras): building FatFs for the card.

These are git submodules pinned to release tags; Dependabot proposes new releases as pull requests.
