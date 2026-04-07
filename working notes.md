# 2/24/26

Output:
```{text}
(crawl-server) alex@alexs-desktop build % ./dcss3d
[2026-02-24 21:20:41.411] [debug] socket path: '/home/alex/Code/dcss3d/v2/build/dcss3d.sock'
[2026-02-24 21:20:41.417] [error] Received unhandled exception: socket connect failure for socket path /home/alex/Code/dcss3d/v2/build/dcss3d.sock: Connection refused
Traceback (most recent call last):
  File "/home/alex/Code/dcss3d/v2/build/dcss_server.py", line 21, in <module>
    from websockets.sync.client import connect
  File "/home/alex/Code/venvs/crawl-server/lib64/python3.14/site-packages/websockets/sync/client.py", line 22, in <module>
    from .connection import Connection
  File "<frozen importlib._bootstrap>", line 1371, in _find_and_load
  File "<frozen importlib._bootstrap>", line 1342, in _find_and_load_unlocked
  File "<frozen importlib._bootstrap>", line 938, in _load_unlocked
  File "<frozen importlib._bootstrap_external>", line 755, in exec_module
  File "<frozen importlib._bootstrap_external>", line 851, in get_code
  File "<frozen importlib._bootstrap_external>", line 951, in get_data
KeyboardInterrupt
```

- I bet the error is because the server subprocess hasn't finished setting up before the c++ code gets here so it crashes when reading the nonexistent socket. We need to loop if it doesn't exist!
