#!python3

import argparse
import json
import logging
import os
import select
import socket
import socketserver
import struct
import sys
import zlib
from collections import deque
from select import POLLERR, POLLHUP, POLLIN, POLLNVAL

from websockets.exceptions import (
    ConnectionClosed,
    ConnectionClosedError,
    ConnectionClosedOK,
)
from websockets.sync.client import connect

logger = logging.getLogger(__name__)

HOST, PORT = "localhost", 8080
# Get socket path relative to script directory
import os
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOCKET = os.path.join(SCRIPT_DIR, "dcss3d.sock")

USERNAME = "asciineuron"
PASSWORD = "password"
# GAME_ID = "dcss-web-trunk"
GAME_ID = "Dungeon Crawl Stone Soup 0.34.0"


def len_encode_msg(msg):
    msg_len = len(msg)
    return struct.pack(f"=I{msg_len}s", msg_len, msg.encode("ascii"))


def len_decode_msg(msg):
    (msg_len,) = struct.unpack("=I", msg[:4])
    msg_body = struct.unpack_from(f"={msg_len}s", msg, offset=4)
    return msg_len, msg_body


class DCSSClient:
    # TODO: for now passes everything to dcss3d, may want to intercept some messages in _has_interesting_next_message()
    # This client abstracts the server into a series of 'msg' dict blobs that are sent to the local game as strings, so it doesn't
    # have to track a queue or expand lists of messages etc.

    WEBSOCK_LINK = "ws://localhost:8080/socket"

    def __init__(self, sock):
        self._websock = connect(DCSSClient.WEBSOCK_LINK, sock=sock)
        self._tcpsock = sock
        # no stream header/trailer expected, hence -zlib.MAX_WBITS
        self._decompressobj = zlib.decompressobj(-zlib.MAX_WBITS)

        # list json msg data as dicts, pop to get next network message
        self._message_dicts = deque()
        self._is_connected = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def close(self):
        self._is_connected = False
        if self._websock:
            try:
                self._websock.close()
            except Exception:
                pass
        self._websock = None

    def is_connected(self):
        return self._is_connected and self._websock is not None

    def __iter__(self):
        return self.messages(as_string=True)

    def login(self, username, password):
        message = json.dumps(
            {"msg": "login", "username": str(username), "password": str(password)}
        )
        self.send(message)

        # expect 'ping', 'lobby_clear', and 'lobby_complete' messages next, before receiving commands:
        _ = self._receive_check_msg("lobby_clear")
        _ = self._receive_check_msg("lobby_complete")
        # 2-25 wait for Play Now message
        self._receive_check_msg("set_game_links", timeout=None)
        # self._receive_play_now()

    # def _receive_play_now(self):
    #     # NOTE: "set_game_links" not html, can maybe use _receive_check_msg() instead
    #     # repeats _receive_check_msg("set_game_links", timeout=None)
    #     # until gets "content" containing 'Play now'
    #     while True: # TODO 2-25 not sure why won't keep looping _receive_check_msg()...
    #         while msg := self._receive_check_msg("set_game_links", timeout=None):
    #             print(f"loop msg: {msg}")
    #             try:
    #                 if "Play now" in msg["content"]:
    #                     print("DONE WAITING!")
    #                     return
    #             except Exception:
    #                 continue

    def play(self, game_id=GAME_ID):
        message = json.dumps({"msg": "play", "game_id": str(game_id)})
        self.send(message)
        # TODO: we sometimes get the game data before the game_started message, need to send it so remove check for 'game_started'
        # self._receive_check_msg("game_started")
        self._choose_character()

    def send(self, message):
        if not self._is_connected or self._websock is None:
            logger.error("Cannot send message: WebSocket not connected")
            return
        try:
            logger.debug(f"sending string: {message}")
            self._websock.send(message)
        except ConnectionClosed as e:
            logger.error(f"Connection closed while sending: {e}")
            self._is_connected = False
        except Exception as e:
            logger.error(f"Error sending message: {e}")
            self._is_connected = False

    def fileno(self):
        # for polling
        return self._tcpsock.fileno()

    def _decode_decompress_server_response(self, response):
        # it seems like it's a bytes object, decompress zlib data and add extra bytes
        return self._decompressobj.decompress(response + bytearray(b"\x00\x00\xff\xff"))

    def _get_decoded_response(self, timeout=None):
        response = self._websock.recv(timeout=timeout, decode=False)
        decoded_response = self._decode_decompress_server_response(response)
        logger.debug(
            f"server response string: {decoded_response}\nwith length: {len(decoded_response)}"
        )
        return decoded_response

    def _get_server_messages(self, timeout=None):
        """Receives a message from the server, decodes it, and appends all json messages to the queue"""
        if not self._is_connected or self._websock is None:
            logger.debug("WebSocket not connected, skipping message receive")
            return
        try:
            response = self._websock.recv(timeout=timeout, decode=False)
            decoded_response = self._decode_decompress_server_response(response)
            logger.debug(
                f"server response string: {decoded_response}\nwith length: {len(decoded_response)}"
            )
            json_response = json.loads(decoded_response)
            if json_response.get("msg") is not None:
                # response is a single message dict
                self._message_dicts.append(json_response)
            else:
                # response is an array of message dicts
                messages = json_response["msgs"]
                self._message_dicts.extend(messages)
        except TimeoutError:
            # Normal timeout, no message available
            pass
        except ConnectionClosed as e:
            logger.error(f"Connection closed while receiving: {e}")
            self._is_connected = False
        except Exception as e:
            logger.error(f"Error receiving server message: {e}")
            self._is_connected = False

    def messages(self, as_string=False, timeout=None):
        """If messsage queue is empty, recv() a new server response, otherwise pop the next saved message dict"""
        try:
            if len(self._message_dicts) == 0:
                self._get_server_messages(timeout)
            msg = self._message_dicts.popleft()
            if as_string:
                yield json.dumps(msg)
            else:
                yield msg
        except TimeoutError:
            logger.debug("no more messages from the server")

    def _receive_check_msg(self, msg_val, timeout=None):
        """Receives a message and throws a ConnectionError exception if 'msg'
        contents don't match msg_val."""
        # just consume one response:
        for msg_dict in self.messages(timeout=timeout):
            # if msg_dict["msg"] != str(msg_val):
            #     raise ConnectionError(f"didn't receive '{msg_val}'")
            # else:
            #     return

            # TODO: changed, now waits here until it gets the message...
            if msg_dict["msg"] == str(msg_val):
                print(f"returning {msg_dict}")
                return msg_dict

    def _send_msg_text(self, text):
        """Sends a {"msg": "input", "text": text} message to server."""
        raise NotImplementedError

    def _choose_character(self):
        """Loop until receive 'ui-push' with 'species', 'background', 'weapon',
        send dummy keypresses to set up Minotaur,Berserker,Hand axe."""
        # TODO: add input options or some way to customize character, also can
        # edit the input rc file
        did_species, did_background, did_weapon = False, False, False
        while not (did_species and did_species and did_weapon):
            for msg_dict in self.messages():
                if msg_dict["msg"] == "ui-push":
                    match msg_dict["type"]:
                        case "species":
                            self._send_msg_text("b")
                            did_species = True
                        case "background":
                            self._send_msg_text("i")
                            did_background = True
                        case "weapon":
                            self._send_msg_text("c")
                            did_weapon = True
                        case _:
                            pass

    # TODO: parse "content" html message for game ids list and other behavior
    # the local game won't implement


class DCSSUnixStreamHandler(socketserver.StreamRequestHandler):
    """Connects to dcss websocket and streams data to/from local dcss3d client."""

    # keeps socket open for duration of game, only needs to handle one request
    RECONNECT_MSG = "__RECONNECT__"

    def _create_client(self):
        """Create a new DCSSClient connection."""
        return DCSSClient(socket.create_connection((HOST, PORT)))

    def handle(self):
        poll = select.poll()
        poll.register(self.request.fileno(), POLLIN | POLLHUP)
        # add debug user input from this stdin:
        poll.register(sys.stdin.fileno(), POLLIN | POLLHUP)

        dcss_client = None
        try:
            dcss_client = self._create_client()
            poll.register(dcss_client.fileno(), POLLIN | POLLHUP)
            logger.info("WebSocket connection established")

            # TODO 2-26 add to c++ instead
            # dcss_client.login(USERNAME, PASSWORD)
            # dcss_client.play()

            while fd_event_list := poll.poll():
                if any(
                    event & (POLLHUP | POLLNVAL | POLLERR) for _, event in fd_event_list
                ):
                    # Check which fd had the event
                    for fd, event in fd_event_list:
                        if event & (POLLHUP | POLLNVAL | POLLERR):
                            if fd == self.request.fileno():
                                logger.info("Client disconnected")
                            elif dcss_client and fd == dcss_client.fileno():
                                logger.info("DCSS server disconnected")
                    break

                # check for game messages
                for game_message in dcss_client.messages(as_string=True, timeout=0):
                    logger.debug(f"sending message to client: {game_message}")
                    encoded_msg = len_encode_msg(game_message)
                    self.wfile.write(encoded_msg)

                for fd, event in fd_event_list:
                    if fd == self.request.fileno():
                        if not event & POLLIN:
                            continue
                        # Check for reconnect message
                        msg_len_bytes = self.rfile.read(4)
                        if len(msg_len_bytes) < 4:
                            logger.debug("Client sent incomplete message length")
                            continue
                        msg_len = int.from_bytes(
                            msg_len_bytes, byteorder=sys.byteorder
                        )
                        if msg_len == len(self.RECONNECT_MSG) + 4:  # 4 bytes for length + message
                            msg_body = self.rfile.read(msg_len).decode("ascii", errors="ignore")
                            if msg_body == self.RECONNECT_MSG:
                                logger.info("Received reconnect request from client")
                                # Clean up old connection
                                if dcss_client:
                                    poll.unregister(dcss_client.fileno())
                                    dcss_client.close()
                                    dcss_client = None
                                # Try to reconnect
                                try:
                                    dcss_client = self._create_client()
                                    poll.register(dcss_client.fileno(), POLLIN | POLLHUP)
                                    logger.info("WebSocket reconnection successful")
                                except Exception as e:
                                    logger.error(f"Reconnection failed: {e}")
                                continue
                        msg = self.rfile.read(msg_len).decode("ascii")
                        logger.debug(
                            f"received client message: {msg}, with len: {msg_len}"
                        )
                        # send to game server:
                        dcss_client.send(msg)
                    # elif fd == sys.stdin.fileno():
                    #     if not event & POLLIN:
                    #         continue
                    #     # read one line, send as text input message
                    #     input_line = sys.stdin.readline().strip()
                    #     input_msg = {"msg": "input", "text": input_line}
                    #     dcss_client.send(json.dumps(input_msg))

        except Exception as e:
            logger.error(f"Handler error: {e}")
        finally:
            if dcss_client:
                dcss_client.close()
            logger.info("Handler cleanup complete")


class FileDCSSUnixStreamServer(socketserver.UnixStreamServer):
    """UnixStreamServer which loads a file with dcss map json data and echoes to the local dcss3d client."""

    def __init__(self, file, *args, **kwargs):
        super(socketserver.UnixStreamServer, self).__init__(*args, **kwargs)
        self.file = open(file)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.file.close()


class FileDCSSUnixStreamHandler(socketserver.StreamRequestHandler):
    """StreamRequestHandler which oads a file with dcss map json data and echoes to the local dcss3d client."""

    def handle(self):
        # load test json file
        file_contents = self.server.file.read()

        poll = select.poll()
        poll.register(self.request.fileno(), POLLIN | POLLHUP)

        while poll_res := poll.poll():
            if any(event & (POLLHUP | POLLNVAL | POLLERR) for _, event in poll_res):
                break

            # send message:
            encoded_msg = len_encode_msg(file_contents)
            # logger.debug(f"sending message: {encoded_msg}")
            logger.debug(
                f"sending json message: {json.dumps(json.loads(file_contents), indent=2)}"
            )
            self.wfile.write(encoded_msg)

            # get client response:
            msg_len = int.from_bytes(self.rfile.read(4), byteorder=sys.byteorder)
            msg = self.rfile.read(msg_len).decode("ascii")
            logger.debug(f"received message: {msg}")


# this script starts a connection to the online dcss server
# as well as a local socket for dcss3d. it then bounces the
# client socket data to the online server, and vice versa
# for server responses

if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)

    parser = argparse.ArgumentParser(
        description="middleman server between websocket dcss and local unix-socket dcss3d instance"
    )
    parser.add_argument(
        "--file",
        help="disable webserver functionality, load json map data from a file instead",
    )
    # parser.add_argument(
    #     "--stream",
    #     help="TODO? don't drive server via game, instead send to local webtile instance, use dcss3d to passively view the current map",
    # )
    args = parser.parse_args()

    logger.info("starting dcss server rerouter")

    if os.path.exists(SOCKET):
        os.unlink(SOCKET)

    stream_server = (
        FileDCSSUnixStreamServer(args.file, SOCKET, FileDCSSUnixStreamHandler)
        if args.file
        else socketserver.UnixStreamServer(SOCKET, DCSSUnixStreamHandler)
    )
    with stream_server as local_server:
        local_server.handle_request()
