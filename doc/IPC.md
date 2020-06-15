pimctl ipc interface
====================

pimdd share pimctl with pimd.  The front-end itself is fairly simple and
independent of either PIM daemon.

The default socket file is /var/run/pimctl.sock if multiple PIM daemons
are used at the same time (possible on Linux), the user have to set the
socket path for both daemons and client.

The API is designed around sending string commands to the daemon over
the socket.  Each command from pimctl results in a, possibly multi-line,
string response from the daemon.

commands
--------

- `usage`: multi-line command + description
  
  The command starts at column 0, description follows on the
  next line and is always indented with white space (TAB)

- `version`: version of daemon, e.g. "pimd v2.3.2"

