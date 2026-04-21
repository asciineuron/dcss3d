#!/usr/bin/env python3
"""
Unit tests for dcss_server.py

These tests verify the message encoding/decoding and basic client functionality
without requiring an actual WebSocket or Unix socket connection.
"""

import json
import struct
import sys
import os
import unittest
from unittest.mock import Mock, patch, MagicMock
from collections import deque

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from websockets.exceptions import ConnectionClosed, ConnectionClosedOK


class TestLenEncodeMsg(unittest.TestCase):
    """Test the length-prefixed message encoding."""

    def test_encode_basic_message(self):
        """Test encoding a simple JSON message."""
        # Import the function from dcss_server
        from dcss_server import len_encode_msg

        msg = '{"msg": "test"}'
        encoded = len_encode_msg(msg)

        # Should be: 4 bytes for length + message bytes
        expected_len = 4 + len(msg)
        self.assertEqual(len(encoded), expected_len)

        # First 4 bytes should be the length in network byte order
        msg_len = struct.unpack("=I", encoded[:4])[0]
        self.assertEqual(msg_len, len(msg))

        # Remaining bytes should be the message
        self.assertEqual(encoded[4:].decode("ascii"), msg)

    def test_encode_long_message(self):
        """Test encoding a longer message."""
        from dcss_server import len_encode_msg

        msg = '{"msg": "long_message", "data": "' + 'x' * 1000 + '"}'
        encoded = len_encode_msg(msg)

        msg_len = struct.unpack("=I", encoded[:4])[0]
        self.assertEqual(msg_len, len(msg))
        self.assertEqual(encoded[4:].decode("ascii"), msg)


class TestLenDecodeMsg(unittest.TestCase):
    """Test the length-prefixed message decoding."""

    def test_decode_basic_message(self):
        """Test decoding a simple encoded message."""
        from dcss_server import len_encode_msg, len_decode_msg

        original_msg = '{"msg": "test"}'
        encoded = len_encode_msg(original_msg)

        msg_len, msg_body = len_decode_msg(encoded)
        self.assertEqual(msg_len, len(original_msg))
        self.assertEqual(msg_body[0].decode("ascii"), original_msg)

    def test_decode_roundtrip(self):
        """Test that encode/decode roundtrip preserves message."""
        from dcss_server import len_encode_msg, len_decode_msg

        messages = [
            '{"msg": "ping"}',
            '{"msg": "input", "text": "5"}',
            '{"msg": "login", "username": "test", "password": "pass"}',
            '{"msg": "map", "data": {"cols": 10, "rows": 20}}',
        ]

        for msg in messages:
            encoded = len_encode_msg(msg)
            _, decoded = len_decode_msg(encoded)
            self.assertEqual(decoded[0].decode("ascii"), msg)


class TestDCSSClientConnectionState(unittest.TestCase):
    """Test DCSSClient connection state management."""

    def setUp(self):
        """Set up mock socket for testing."""
        # Import after path is set
        import dcss_server

        # Create a mock socket
        self.mock_sock = Mock()

        # Mock the websocket connect function
        self.mock_websock = Mock()

        with patch('dcss_server.connect') as mock_connect:
            mock_connect.return_value = self.mock_websock
            self.client = dcss_server.DCSSClient(self.mock_sock)

    def test_initial_connection_state(self):
        """Test that client starts in connected state."""
        self.assertTrue(self.client.is_connected())

    def test_close_sets_disconnected(self):
        """Test that close() sets connection state to disconnected."""
        self.client.close()
        self.assertFalse(self.client.is_connected())

    def test_double_close_is_safe(self):
        """Test that calling close() twice doesn't raise an error."""
        self.client.close()
        self.client.close()  # Should not raise
        self.assertFalse(self.client.is_connected())

    def test_exit_calls_close(self):
        """Test that __exit__ properly closes the connection."""
        with patch.object(self.client, 'close') as mock_close:
            self.client.__exit__(None, None, None)
            mock_close.assert_called_once()


class TestDCSSClientSend(unittest.TestCase):
    """Test DCSSClient send functionality."""

    def setUp(self):
        """Set up mock socket for testing."""
        import dcss_server

        self.mock_sock = Mock()
        self.mock_websock = Mock()

        with patch('dcss_server.connect') as mock_connect:
            mock_connect.return_value = self.mock_websock
            self.client = dcss_server.DCSSClient(self.mock_sock)

    def test_send_success(self):
        """Test successful message sending."""
        test_message = '{"msg": "test"}'
        self.client.send(test_message)
        self.mock_websock.send.assert_called_once_with(test_message)

    def test_send_when_disconnected(self):
        """Test that send() does nothing when disconnected."""
        self.client.close()
        # Should not raise, just log and return
        self.client.send('{"msg": "test"}')
        self.mock_websock.send.assert_not_called()

    def test_send_handles_connection_closed(self):
        """Test that send() handles ConnectionClosed exception."""
        self.mock_websock.send.side_effect = ConnectionClosed(None, None)

        # Should not raise, should set disconnected state
        self.client.send('{"msg": "test"}')
        self.assertFalse(self.client.is_connected())

    def test_send_handles_generic_exception(self):
        """Test that send() handles generic exceptions."""
        self.mock_websock.send.side_effect = Exception("Network error")

        # Should not raise, should set disconnected state
        self.client.send('{"msg": "test"}')
        self.assertFalse(self.client.is_connected())


class TestDCSSClientReceive(unittest.TestCase):
    """Test DCSSClient message receiving functionality."""

    def setUp(self):
        """Set up mock socket for testing."""
        import dcss_server

        self.mock_sock = Mock()
        self.mock_websock = Mock()

        with patch('dcss_server.connect') as mock_connect:
            mock_connect.return_value = self.mock_websock
            self.client = dcss_server.DCSSClient(self.mock_sock)

    def test_get_server_messages_success(self):
        """Test successful message receiving."""
        import zlib

        test_response = json.dumps({"msg": "ping", "data": "test"})
        compressed = zlib.compress(test_response.encode("ascii"))[2:-4]  # Strip zlib header/trailer

        self.mock_websock.recv.return_value = compressed

        self.client._get_server_messages()

        # Message should be in the queue
        messages = list(self.client.messages(timeout=0))
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0]["msg"], "ping")

    def test_get_server_messages_when_disconnected(self):
        """Test that receive does nothing when disconnected."""
        self.client.close()
        self.mock_websock.recv.assert_not_called()

        # Should not raise
        self.client._get_server_messages()

    def test_get_server_messages_handles_timeout(self):
        """Test that receive handles TimeoutError gracefully."""
        import socket

        self.mock_websock.recv.side_effect = socket.timeout()

        # Should not raise
        self.client._get_server_messages()
        self.assertTrue(self.client.is_connected())

    def test_get_server_messages_handles_connection_closed(self):
        """Test that receive handles ConnectionClosed gracefully."""
        self.mock_websock.recv.side_effect = ConnectionClosed(None, None)

        # Should not raise, should set disconnected
        self.client._get_server_messages()
        self.assertFalse(self.client.is_connected())


class TestReconnectMessage(unittest.TestCase):
    """Test the reconnect message handling."""

    def test_reconnect_msg_constant_exists(self):
        """Test that RECONNECT_MSG constant is defined."""
        import dcss_server

        self.assertTrue(hasattr(dcss_server.DCSSUnixStreamHandler, 'RECONNECT_MSG'))
        self.assertEqual(dcss_server.DCSSUnixStreamHandler.RECONNECT_MSG, "__RECONNECT__")

    def test_reconnect_msg_length(self):
        """Test that reconnect message has expected length."""
        import dcss_server

        reconnect_msg = dcss_server.DCSSUnixStreamHandler.RECONNECT_MSG
        self.assertEqual(len(reconnect_msg), 13)


class TestMessageQueueHandling(unittest.TestCase):
    """Test message queue management in DCSSClient."""

    def setUp(self):
        """Set up mock socket for testing."""
        import dcss_server

        self.mock_sock = Mock()
        self.mock_websock = Mock()

        with patch('dcss_server.connect') as mock_connect:
            mock_connect.return_value = self.mock_websock
            self.client = dcss_server.DCSSClient(self.mock_sock)

    def test_messages_generator_yields_strings(self):
        """Test that messages() yields string representations when requested."""
        import zlib

        test_response = json.dumps({"msg": "test", "data": "value"})
        compressed = zlib.compress(test_response.encode("ascii"))[2:-4]

        self.mock_websock.recv.return_value = compressed

        # Get messages as strings
        self.client._get_server_messages()
        messages = list(self.client.messages(as_string=True, timeout=0))

        self.assertEqual(len(messages), 1)
        self.assertIsInstance(messages[0], str)
        self.assertIn('"msg": "test"', messages[0])

    def test_messages_generator_yields_dicts(self):
        """Test that messages() yields dict representations when requested."""
        import zlib

        test_response = json.dumps({"msg": "test", "data": "value"})
        compressed = zlib.compress(test_response.encode("ascii"))[2:-4]

        self.mock_websock.recv.return_value = compressed

        # Get messages as dicts
        self.client._get_server_messages()
        messages = list(self.client.messages(as_string=False, timeout=0))

        self.assertEqual(len(messages), 1)
        self.assertIsInstance(messages[0], dict)
        self.assertEqual(messages[0]["msg"], "test")


if __name__ == "__main__":
    unittest.main()
