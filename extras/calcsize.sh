# calcsize -- tell Linux the terminal's size by asking the terminal.
#
# For ~/.bashrc on the host calcterm logs in to.  It sends the cursor to
# the far corner, asks where it got to (ESC [ 6 n), and sets the tty from
# the answer, so it works at 64 or 80 columns, or in any VT100.  Run it
# again after Alt+F1 switches the width.
calcsize() {
	local old row col
	old=$(stty -g)
	stty raw -echo min 0 time 10
	printf '\0337\033[999;999H\033[6n\0338' >/dev/tty
	IFS='[;R' read -r -d R _ row col </dev/tty
	stty "$old"
	if [ -n "$row" ] && [ -n "$col" ]; then
		stty rows "$row" cols "$col"
	fi
}
case "$(tty)" in /dev/tty[SA]*|/dev/ttyUSB*|/dev/ttyACM*) calcsize ;; esac
