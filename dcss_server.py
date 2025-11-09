#!/usr/bin/env python3

import sys
import os
import socketserver
import struct
import logging
import select
import zlib
import json
import argparse
from websockets.sync.client import connect
from websockets.exceptions import (
    ConnectionClosed,
    ConnectionClosedOK,
    ConnectionClosedError,
)

logger = logging.getLogger(__name__)

HOST, PORT = "localhost", 9999
SOCKET = "./build/sdlproj1.sock"

USERNAME = "asciineuron"
PASSWORD = "password"
GAME_ID = "dcss-web-trunk"


def len_encode_msg(msg):
    msg_len = len(msg)
    return struct.pack(f"=I{msg_len}s", msg_len, msg.encode("ascii"))


def len_decode_msg(msg):
    (msg_len,) = struct.unpack("=I", msg[:4])
    msg_body = struct.unpack_from(f"={msg_len}s", msg, offset=4)
    return msg_len, msg_body


class DCSSClient:
    # TODO: set up the iterator so we get through the lobby into the actual game loop first
    # TODO: handle all "ui-push" responses since the game can't handle these yet

    WEBSOCK_LINK = "ws://localhost:8080/socket"

    def __init__(self):
        self._websock = connect(DCSSClient.WEBSOCK_LINK)
        # no stream header/trailer expected, hence -zlib.MAX_WBITS
        self._decompressobj = zlib.decompressobj(-zlib.MAX_WBITS)

        # self._current_response = ""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self._websock.close()

    def __iter__(self):
        # return self
        yield self._get_next_game_response()

    # def __next__(self):
        # try:
        #     return self._get_next_game_response()
        # except ConnectionClosedOk as e:
        #     raise StopIteration
        # except Exception as e:
        #     raise

    def login(self, username, password):
        message = json.dumps(
            {"msg": "login", "username": str(username), "password": str(password)}
        )
        self._websock.send(message)
        # expect 'ping', 'lobby_clear', and 'lobby_complete' messages next, before receiving commands:
        self._receive_check_msg("ping")
        self._receive_check_msg("lobby_clear")
        self._receive_check_msg("lobby_complete")

    def play(self, game_id="dcss-web-trunk"):
        message = json.dumps({"msg": "play", "game_id": str(game_id)})
        self._websock.send(message)
        self._receive_check_msg("game_started")
        self._choose_character()

    def send(self, message):
        self._websock.send(message)

    def _decode_decompress_server_response(self, response):
        # it seems like it's a bytes object, decompress zlib data and add extra bytes
        return self._decompressobj.decompress(response + bytearray(b"\x00\x00\xff\xff"))

    # TODO extract each msg json object to store a stack of them, handle smartly
    # then reconvert to string when sending to the dcs3d app. this way we can smartly
    # handle a per-message logic here vs a giant text block
    def _get_decoded_response(self):
        response = self._websock.recv(decode=False)
        decoded_response = self._decode_decompress_server_response(response)
        logger.debug(f"server response string: {decoded_response}")
        return decoded_response
        # self._current_response = decoded_response

    def _iterate_msgs(self, response):
        """Server response sometimes contains many 'msg' messages in a 'msgs'
        array. If so, loop over this instead, since toplevel msg not present."""
        json_response = json.loads(response)
        if msg_val := json_response.get("msg"):
            yield msg_val
        else:
            messages = json_response["msgs"]
            yield from messages
            # for msg in messages:
            #     yield msg["msg"]

    def _get_next_game_response(self):
        """Messages with "msg":"ui-push" are for the web-interface and aren't
        currently supported by dcss3d, skip all such messages."""
        # TODO: what if multiple important messages are given at once? 
        # ^ We should turn this into an iterator or return a list of messages
        while response := self._get_decoded_response():
            # json_msg = json.loads(response)["msg"]
            # if json_msg != "ui-push":
            #     return response
            # else:
            #     logger.info(f"UI-PUSH message: {json_msg}")
            for msg_dict in self._iterate_msgs(response):
                if msg_dict["msg"] != "ui-push":
                    # return response
                    yield response
                else:
                    logger.info(f"UI-PUSH message: {json_msg}")

    def _receive_check_msg(self, msg_val):
        """Receives a message and throws a ConnectionError exception if 'msg'
        contents don't match msg_val."""
        response = self._get_decoded_response()
        # json_msg = json.loads(response)["msg"]
        # if json_msg != str(msg_val):
        #     raise ConnectionError(f"didn't receive '{msg_val}'")
        for msg_dict in self._iterate_msgs(response):
            if msg_dict["msg"] != str(msg_val):
                raise ConnectionError(f"didn't receive '{msg_val}'")

    def _send_msg_text(self, text):
        """Sends a {"msg": "input", "text": text} message to server."""
        pass

    def _choose_character(self):
        """Loop until receive 'ui-push' with 'species', 'background', 'weapon',
        send dummy keypresses to set up Minotaur,Berserker,Hand axe."""
        # TODO: add input options or some way to customize character, also can
        # edit the input rc file
        did_species, did_background, did_weapon = False, False, False
        while not (did_species and did_species and did_weapon):
            # json_response = json.loads(self._get_decoded_response())
            # if json_response["msg"] == "ui-push":
                # match json_response["type"]:
                #     case "species":
                #         self._send_msg_text("b")
                #         did_species = True
                #     case "background":
                #         self._send_msg_text("i")
                #         did_background = True
                #     case "weapon":
                #         self._send_msg_text("c")
                #         did_weapon = True
                #     case _:
                #         pass
            for msg_dict in self._iterate_msgs(self._get_decoded_response()):
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

    # TODO: parse "content" html message for game ids list


class DCSSUnixStreamHandler(socketserver.StreamRequestHandler):
    """Connects to dcss websocket and streams data to/from local dcss3d client."""

    # TODO: bad design, new class created for each request, prob don't want to spin up webserver connection?

    # def setup(self):
    #     super(socketserver.StreamRequestHandler, self).setup()
    #     self.f = open(TEST_JSON_MAP, 'r')
    #     self.file_contents = self.f.read()

    # def finish(self):
    #     super(socketserver.StreamRequestHandler, self).finish()
    #     self.f.close()

    def handle(self):
        poll = select.poll()
        poll.register(self.request.fileno(), select.POLLIN | select.POLLHUP)

        with DCSSClient() as dcss_client:
            dcss_client.login(USERNAME, PASSWORD)
            dcss_client.play()

            # iterate through game responses:
            for game_message in dcss_client:
                # send message to client:
                encoded_msg = len_encode_msg(game_message)
                logger.debug(f"received server message: {encoded_msg}")
                self.wfile.write(encoded_msg)

                # wait for local game update, quitting if received hangup or error:
                if any(
                    event & (select.POLLHUP | select.POLLNVAL | select.POLLERR)
                    for _, event in poll.poll()
                ):
                    break

                # get client response:
                msg_len = int.from_bytes(self.rfile.read(4), byteorder=sys.byteorder)
                msg = self.rfile.read(msg_len).decode("ascii")
                logger.debug(f"received client message: {msg}")

                # send to game server:
                dcss_client.send(msg)


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
        poll.register(self.request.fileno(), select.POLLIN | select.POLLHUP)

        while poll_res := poll.poll():
            if any(
                event & (select.POLLHUP | select.POLLNVAL | select.POLLERR)
                for _, event in poll_res
            ):
                break

            # send message:
            encoded_msg = len_encode_msg(file_contents)
            # logger.debug(f"sending message: {encoded_msg}")
            logger.debug(f"sending json message: {json.dumps(json.loads(file_contents), indent=2)}")
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
    parser.add_argument(
        "--stream",
        help="TODO? don't drive server via game, instead send to local webtile instance, use dcss3d to passively view the current map",
    )
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
