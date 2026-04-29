#!python3

import argparse
import asyncio
import json
import logging
import os
import struct
import zlib

import websockets.asyncio.client as websockets_client
from websockets.exceptions import ConnectionClosed

logger = logging.getLogger(__name__)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOCKET = os.path.join(SCRIPT_DIR, "dcss3d.sock")
WEBSOCK_LINK = "ws://localhost:8080/socket"


def len_encode_msg(msg: str) -> bytes:
    msg_len = len(msg)
    return struct.pack(f"=I{msg_len}s", msg_len, msg.encode("ascii"))


# ── asyncio-based relay ──────────────────────────────────────────────────────

async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    """Bridge between the C++ Unix-socket client and the DCSS websocket server.

    Returns True if the client requested a reconnect (__RECONNECT__),
    False if the client disconnected."""

    reconnect_requested = False

    # no stream header/trailer expected, hence -zlib.MAX_WBITS
    decompressobj = zlib.decompressobj(-zlib.MAX_WBITS)

    async with websockets_client.connect(WEBSOCK_LINK) as websock:
        logger.info("WebSocket connection established")

        async def cpp_to_dcss():
            """Read length-prefixed JSON from C++, forward to websocket."""
            nonlocal reconnect_requested
            while True:
                try:
                    msg_len_bytes = await reader.readexactly(4)
                except asyncio.IncompleteReadError:
                    logger.info("Client disconnected")
                    return
                msg_len = struct.unpack("=I", msg_len_bytes)[0]
                msg_data = await reader.readexactly(msg_len)
                msg = msg_data.decode("ascii")
                logger.debug(f"received client message: {msg}, with len: {msg_len}")

                # Handle reconnect signal
                if msg == "__RECONNECT__":
                    logger.info("Received reconnect request, closing websocket")
                    reconnect_requested = True
                    return  # exit task, outer handler will reconnect

                logger.debug(f"sending string: {msg}")
                await websock.send(msg)

        async def dcss_to_cpp():
            """Read compressed websocket frames, decompress, forward to C++."""
            while True:
                try:
                    response = await websock.recv(decode=False)
                except ConnectionClosed:
                    logger.info("DCSS server disconnected")
                    return

                decoded = decompressobj.decompress(
                    response + b"\x00\x00\xff\xff"
                )
                json_response = json.loads(decoded)

                # Single message or batch
                if json_response.get("msg") is not None:
                    msg_dicts = [json_response]
                    logger.debug(f"server msg type: {json_response.get('msg')}, len={len(decoded)}")
                else:
                    msg_dicts = json_response["msgs"]
                    for m in msg_dicts:
                        logger.debug(f"server msg type (batch): {m.get('msg')}")

                for msg in msg_dicts:
                    msg_str = json.dumps(msg)
                    logger.debug(f"forwarding to C++: msg={msg.get('msg')}, len={len(msg_str)}")
                    writer.write(len_encode_msg(msg_str))
                    await writer.drain()

        # Run both directions concurrently; either exiting triggers cleanup
        task_c2d = asyncio.create_task(cpp_to_dcss())
        task_d2c = asyncio.create_task(dcss_to_cpp())
        done, pending = await asyncio.wait(
            [task_c2d, task_d2c],
            return_when=asyncio.FIRST_COMPLETED,
        )
        for task in pending:
            task.cancel()

    return reconnect_requested


async def handle_client_with_reconnect(reader, writer):
    """Handle client with automatic websocket reconnection on __RECONNECT__."""
    while True:
        try:
            should_reconnect = await handle_client(reader, writer)
        except Exception as e:
            logger.error(f"Handler error: {e}")
            should_reconnect = False
        if not should_reconnect:
            break
        logger.info("Attempting websocket reconnect...")
        await asyncio.sleep(0.5)


# ── file-based test handler ──────────────────────────────────────────────────

async def handle_file_client(reader, writer, file_contents):
    """Echo a static JSON file to the C++ client (for testing)."""
    encoded_msg = len_encode_msg(file_contents)
    writer.write(encoded_msg)
    await writer.drain()

    # Read one response from client
    try:
        msg_len_bytes = await reader.readexactly(4)
        msg_len = struct.unpack("=I", msg_len_bytes)[0]
        msg_data = await reader.readexactly(msg_len)
        logger.debug(f"received message: {msg_data.decode('ascii')}")
    except asyncio.IncompleteReadError:
        pass


def make_file_handler(file_contents: str):
    """Return a handler coroutine that serves static file data."""
    async def handler(reader, writer):
        await handle_file_client(reader, writer, file_contents)
    return handler


# ── entry point ──────────────────────────────────────────────────────────────

async def main(file_path: str | None = None):
    logger.info("starting dcss server rerouter")

    if os.path.exists(SOCKET):
        os.unlink(SOCKET)

    if file_path:
        with open(file_path) as f:
            file_contents = f.read()
        handler_fn = make_file_handler(file_contents)
    else:
        handler_fn = handle_client_with_reconnect

    async def client_handler(reader, writer):
        try:
            await handler_fn(reader, writer)
        finally:
            server.close()

    server = await asyncio.start_unix_server(client_handler, SOCKET)
    logger.info(f"listening on {SOCKET}")

    async with server:
        try:
            await server.serve_forever()
        except asyncio.CancelledError:
            pass  # server.close() was called by the client handler

    if os.path.exists(SOCKET):
        os.unlink(SOCKET)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)

    parser = argparse.ArgumentParser(
        description="middleman server between websocket dcss and local unix-socket dcss3d instance"
    )
    parser.add_argument(
        "--file",
        help="disable webserver functionality, load json map data from a file instead",
    )
    args = parser.parse_args()

    try:
        asyncio.run(main(file_path=args.file))
    except KeyboardInterrupt:
        pass
