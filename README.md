# calcterm

A VT100 terminal for the PicoCalc (RP2350, Pico 2 W). It talks over a serial line, or by telnet over Wi-Fi; SSH and SFTP are to follow.

The screen is 32 rows of 64 columns, in Blair Leduc's 5×10 font with the VT100's line-drawing characters added so boxes join up, or of 80 columns in calcterm's own 4×10 font. Alt+F1 switches between them.

## What it does

- **VT100**, after the VT100 User Guide: cursor movement, erasing, scrolling regions, origin mode, tab stops, insert and delete of characters and lines, double-height lines excepted. It answers the host's questions (DA, DSR, the cursor-position report).
- **VT52 mode**, entered and left as the VT100 does (`ESC [ ? 2 l`, `ESC <`).
- **What Linux sends anyway**: the 16 colours (256-colour and RGB requests are matched to the nearest of them), bold, underline, reverse, the alternate screen that `vim`, `less` and `htop` use, and UTF-8. Box-drawing characters arrive as UTF-8 from most programs today and are shown with the line-drawing glyphs; other non-ASCII characters show as `?`.
- **ANSI art**, as BBSes draw it: the block characters (full, half and quarter blocks, and the three shades) are drawn as shapes that join up cell to cell, whether they come as UTF-8 or in the IBM PC's own character set (code page 437), which many boards still send. Bytes that can't be UTF-8 are taken as the PC's.
- **Anything it doesn't know is ignored**, window titles included, so it never leaves litter on the screen.
- **Telnet over Wi-Fi** (Pico 2 W), to hosts kept in a list on the card (below).
- **A Bluetooth keyboard** alongside the PicoCalc's own (Pico 2 W).
- **Files both ways by ZMODEM**: `sz` on the host sends one to the PicoCalc's card, `rz` fetches one from it (below), over serial or telnet.

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
| Alt+F1 | 64 or 80 columns (then, on a serial line, run `calcsize` on the host, below; telnet tells the host itself) |
| Alt+F2 | telnet: the host list, to connect or hang up |
| Alt+F3 | the Bluetooth keyboard's state, as a line in the terminal (for troubleshooting) |
| Alt+F4 | a screenshot, to `/screenshots` on the card (a .bmp, and a .txt of the screen's text) |
| **Alt+F5** | **the setup menu** |

## The setup menu (Alt+F5)

Port, speed, format (8N1, 7E1 and so on), XON/XOFF, local echo, what Enter sends, what Backspace sends, and the width. Each line has a highlighted letter that changes or does it in one key (P for the port, S the speed, W the width, T a screenshot, Q to quit calcterm for the PicoCalc's own shell, and so on); the arrow keys work too. Changes take effect at once. *Save these settings* writes them to `/calcterm/calcterm.cfg` on the card (a folder of its own, as the PicoCalc's programs keep theirs), a plain text file that can be edited by hand. The menu also sends a break, resets the terminal, and quits calcterm for the PicoCalc's shell (the host's session carries on, to be picked up next time).

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

## Telnet (Pico 2 W)

Alt+F2 opens a list of hosts. Pick one and calcterm joins the Wi-Fi network, connects, and the line is telnet until you hang up (Alt+F2 again) or the host does; then it's back on the serial port. Esc stops a connection that's taking too long.

**The Wi-Fi network** is the one in `/ntp/ntp.cfg`, the file the PicoCalc shell's `NTP` command uses, so it's set up in one place. If you don't have it yet, write it with the shell's EDIT (or vi):

    ssid     = Home                 # the network's name
    password = "my # password"      # left out for an open network
    country  = US                   # Wi-Fi's country; worldwide if left out

calcterm only reads it. (The shell's user guide has the details: quotes, comments, the `timeout` line.)

**The host list** is `/calcterm/hosts`, a line for each host: a name, its address, and the port if it isn't 23. `#` starts a comment.

    # name     address          port
    mini       192.168.1.50
    bbs        bbs.example.org  2323

Or choose *Type an address...* in the list: `192.168.1.50`, or `bbs.example.org:2323`. One that connects is added to the file for next time. Names are looked up by DNS; names ending in `.local` aren't, so use the address for those.

**What telnet tells the host:** the terminal type (`calcterm`, so install the terminfo entry below on the host), and the screen's size, again whenever Alt+F1 changes it, so `calcsize` isn't needed over telnet. The host echoes what you type; one that says it won't gets calcterm's own echo. ZMODEM works over telnet just as over serial.

**A telnet server on Ubuntu:**

    sudo apt install telnetd

That's all: it installs a small inetd that answers on port 23 and starts a login for each connection. Log in as yourself (Ubuntu doesn't allow root over telnet).

Telnet sends everything as it is, password included, so keep it to your own network: don't forward port 23 on your router. SSH, which encrypts, is planned.

## A Bluetooth keyboard (Pico 2 W)

Pair the keyboard once with `btpair` from [picocalc-extras](https://github.com/ctorkildson-netizen/picocalc-extras) (2.3 or later). After that calcterm finds it by itself: the sign-on says which keyboard it's waiting for, and pressing a key on it connects it ("Keyboard: ... connected" on the bottom line). Both keyboards work at once, and every key does on the Bluetooth one what it does on the PicoCalc's, Alt+F1 to Alt+F5 included. A Bluetooth keyboard works with telnet's Wi-Fi on at the same time; they share the radio.

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

and press Enter on the PicoCalc for the login prompt.

Over the USB-C cable, `ttyUSB0` is the PicoCalc's own USB chip, so it goes away whenever the PicoCalc is unplugged or switched off, and the login with it; when it comes back, nothing starts the login again. A udev rule does that, every time the port appears:

    sudo tee /etc/udev/rules.d/99-calcterm-getty.rules <<'EOF'
    ACTION=="add", SUBSYSTEM=="tty", KERNEL=="ttyUSB0", TAG+="systemd", ENV{SYSTEMD_WANTS}+="serial-getty@ttyUSB0.service"
    EOF
    sudo udevadm control --reload

(A USB-to-RS-232 adapter on the Linux machine stays plugged in, so it doesn't need this.)

A serial line doesn't tell Linux the screen's size, so it has to be set on the PicoCalc's line once you've logged in. `extras/calcsize.sh` does it by asking calcterm: it moves the cursor to the far corner, asks where it got to, and sets the size from the answer, so it's right at 64 columns or 80. Copy it to the Linux machine and add it to the end of `~/.bashrc`:

    cat calcsize.sh >> ~/.bashrc

It runs by itself at each login on a serial line, and `calcsize` sets the size again after Alt+F1 changes the width. Without it, type on the PicoCalc (not on the Linux machine's own screen, which has a size of its own):

    stty rows 32 cols 64        # or cols 80

If Midnight Commander's top line scrolls off the screen, or its key bar wraps, this is what's missing. Programs already running when the width changes carry on at the old one until they're restarted.

If the Up arrow moves the cursor rather than recalling the last command, the login shell is plain `sh`: `chsh -s /bin/bash`. `sudo systemctl stop serial-getty@ttyUSB0.service` ends it.

#### Log out before you leave

A serial line can't tell Linux that the other end has gone. When calcterm quits (to start another program, say), or the cable or RS-232 box comes out, **the login stays open**, and whoever next opens that port, with any terminal program, is in your account without a password. Coming back to calcterm puts you straight back in the same session, which is handy, but it works for anyone.

So log out (`exit`, or Ctrl+D) before leaving calcterm. As a safety net, have bash log out by itself after a while with nothing typed, on serial lines only, by adding this to `~/.bashrc` on the Linux machine:

    case "$(tty)" in /dev/ttyUSB*|/dev/ttyACM*|/dev/ttyS*) TMOUT=900 ;; esac   # log out after 15 idle minutes

It only counts time at the prompt: a long-running program (vim, mc, top) isn't interrupted.

### Midnight Commander

Typing long shell commands on the PicoCalc's keyboard is a chore. [Midnight Commander](https://midnight-commander.org/) (`mc`) is a full-screen file manager in the style of Norton Commander: two directory panels, the arrow keys to move, Enter to open or run, and the function keys along the bottom to view, edit, copy, move, make folders and delete. It fits calcterm well, and makes the PicoCalc a comfortable way to look after another machine:

    sudo apt install mc
    mc

Its command line is still there at the bottom for when you need it, and F10 leaves it.

### Moving files (ZMODEM)

For a short script or a config file, there's no need for anything more than the terminal. The host needs lrzsz, which many Linux systems (Ubuntu among them) already have; if `sz` isn't found:

    sudo apt install lrzsz

**To the PicoCalc:** `sz deploy.sh` at the host's prompt. calcterm sees the transfer start and takes the file, into `/calcterm/files` on the card (replacing one of the same name); the bottom line shows how it's going, and says when it's there. Several files at once work too: `sz *.sh`.

**From the PicoCalc:** `rz` at the host's prompt. calcterm opens a list of the card's files, starting in `/calcterm/files`: the arrows choose, Enter sends the file (or opens a folder), Backspace goes up a folder. The file arrives in the host's current folder, replacing one of the same name. The bottom line says "Sent" as soon as it's there; the host's prompt can take another two or three seconds to come back while rz closes up.

**BOTH WAYS, A FILE OF THE SAME NAME IS REPLACED WITHOUT ASKING:** on the card by `sz`, and in the host's folder by `rz`. That's deliberate (it's for small files, sent back and forth while you work on them), but there's no undo.

It's a serial line, so it's for small files. At 115200 (calcterm's speed unless you've changed it), roughly:

| File | Takes |
| --- | --- |
| a script, 10 KB | about a second |
| 100 KB | about 10 seconds |
| 1 MB | about 1½ minutes |
| 10 MB | about 15 minutes |

At 9600 it's twelve times as long. For anything big, the card in a reader or FTP is the better way.

Esc stops a transfer at any point, and in the list, sends nothing. A damaged packet is sent again, so a noisy line slows a transfer down rather than spoiling it. The line has to carry all eight bits (8N1, as it is unless changed in the setup menu).

### Other tests

`vim`, `less`, `htop`, `top` and `vttest` are good ones (`vttest 32x64` at 64 columns; at 80 its own 80-column screens fit too).

## Building

With `arm-none-eabi-gcc` and a Pico SDK newer than 2.2.0: the Bluetooth keyboard needs the BTstack on the SDK's `develop` branch, which has the host for BLE keyboards (2.2.0's doesn't).

    git clone --recurse-submodules https://github.com/ctorkildson-netizen/calcterm
    cd calcterm
    cmake -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
    ninja -C build                 # build/calcterm.uf2

Copy `calcterm.uf2` to the card's `pico2-apps` folder and start it from UF2 Loader or the shell.

## Tests

`host/` has the terminal's tests, which run on Linux: `make -C host check`. `test_vt` checks the escape sequences one by one. `ptyrun` runs a real program against the terminal and prints the screen it leaves. `screentest` does the same through the renderer, against a model of the LCD and its scrolling, and checks the result matches a fresh whole-screen draw pixel for pixel. `zmtest` sends files both ways between calcterm's ZMODEM and the real `sz` and `rz` (from lrzsz), on a clean line and on one that damages a byte now and then, and compares them byte for byte; `xfertest` does it through the PicoCalc side as well, the card, the keys and the file list. `tntest` runs calcterm's telnet against a real telnetd (started by `tnserve.py`): the terminal type and screen size the host is given, 255 bytes in the data, and ZMODEM both ways over the connection.

## Built on

- [picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter) (Blair Leduc): the LCD, keyboard and SD card drivers, and the font.
- [picocalc-shell](https://github.com/BlairLeduc/picocalc-shell) (Blair Leduc): returning to the shell, and reading `/ntp/ntp.cfg` for Wi-Fi.
- [picocalc-extras](https://github.com/ctorkildson-netizen/picocalc-extras)' btkbd: the Bluetooth keyboard.
- The Pico SDK's CYW43 driver, lwIP and BTstack: Wi-Fi and TCP for telnet, and Bluetooth.
- [picocalc-extras](https://github.com/ctorkildson-netizen/picocalc-extras): building FatFs for the card.

These are git submodules pinned to release tags; Dependabot proposes new releases as pull requests.
