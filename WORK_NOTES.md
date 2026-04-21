# Socket Hangup and Messaging Issue

## Status: RESOLVED

## Problem Statement
The C++ game experienced socket hangup errors and messages never made it to the Python relay server. The Python server exited with KeyboardInterrupt after detecting WebSocket connection closure.

## Root Cause
1. The DCSS WebSocket server closed the connection (timeout or other reason)
2. Python server's `DCSSClient._get_server_messages()` threw `ConnectionClosed` exception (not caught)
3. Uncaught exception caused handler to fail, Python's socketserver closed the Unix socket
4. C++ `poll()` detected `POLLHUP` and logged "socket hangup"
5. Game continued but messages were silently dropped

## Resolution
### Python Server (`dcss_server.py`)
- Added `_is_connected` flag and `is_connected()` method to `DCSSClient`
- Added `close()` method for proper connection cleanup
- Added exception handling in `send()` and `_get_server_messages()` for `ConnectionClosed` and other exceptions
- Updated `DCSSUnixStreamHandler.handle()` to handle disconnect gracefully and `__RECONNECT__` messages

### C++ Game (`src/MessageQueue.cpp`, `src/MessageQueue.hpp`)
- Added `reconnect()` method to `NetworkManager`

### C++ Game UI (`src/imguilayouts.cpp`)
- Added connection status indicator (green "Connected" / red "Disconnected")
- Added "Reconnect" button in network menu

## Verification
- C++ compilation: ✅ Successful
- Python syntax: ✅ Valid
- Unit tests: ✅ 20 passed
- Playtesting: ✅ Confirmed game works as intended

## Git Commit
Commit: 9428818 (fix/socket-hangup-and-messaging-issue merged to main)