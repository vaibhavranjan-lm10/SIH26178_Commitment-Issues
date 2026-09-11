"""MockRouter — a small, TEST-ONLY stand-in for the real `arduino-router`
daemon that ships on the UNO Q's Debian image.

It is not part of Code B and never runs on the board. It exists so the
message CONTRACT between code_b_head_node/mcu/ and code_b_head_node/linux/
can be integration-tested on a dev machine: both real peers (the official
Python `arduino_router_bridge.Bridge` and the native C bridge simulator in
../../mcu/tests/native_bridge_sim/) connect to it exactly as they would to
the real router, over the officially-documented tcp://host:port
development address (see arduino.router_bridge.transport — unix:// is for
the real board only).

Router behaviour it reproduces, because the integration test needs it to
be genuinely representative, not a shortcut that only looks like routing:

  * $/register (REQUEST) binds a method name to whichever connection asked;
    a second connection registering the same name gets ROUTE_ALREADY_EXISTS_ERR,
    matching arduino.router_bridge.connection._register's own handling of it.
  * $/unregister (REQUEST) releases a binding, METHOD_NOT_AVAILABLE_ERR if
    the name was never bound (or was bound to someone else).
  * An application REQUEST for a registered method is forwarded to the
    owning connection with a router-local msgid, so responses route back to
    the true caller even though each connection has its own msgid namespace.
  * An application NOTIFICATION for a registered method is forwarded
    unchanged; for an unregistered one it is silently dropped, exactly as a
    real router has nowhere to send it.
  * A REQUEST for an unregistered method gets an immediate METHOD_NOT_AVAILABLE_ERR
    response, without ever reaching a peer.
"""
from __future__ import annotations

import logging
import socket
import socketserver
import threading

import msgpack

REQUEST, RESPONSE, NOTIFICATION = 0, 1, 2
METHOD_NOT_AVAILABLE_ERR = 0x02
ROUTE_ALREADY_EXISTS_ERR = 0x05

log = logging.getLogger("mock_router")


class _Conn:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.lock = threading.Lock()
        self.registered: set[str] = set()

    def send(self, msg: list) -> None:
        data = msgpack.packb(msg)
        with self.lock:
            self.sock.sendall(data)


class MockRouter:
    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._registry: dict[str, _Conn] = {}
        self._registry_lock = threading.Lock()
        self._pending: dict[int, tuple[_Conn, int]] = {}
        self._pending_lock = threading.Lock()
        self._next_router_id = 1

        router = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                router._serve_connection(self.request)

        class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
            allow_reuse_address = True
            daemon_threads = True

        self._server = Server((host, port), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return self._server.server_address[1]

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()

    def wait_for_registration(self, method_names, timeout: float = 5.0) -> bool:
        """Blocks until every name in ``method_names`` has a registered
        provider (or ``timeout`` elapses). Bridge.connect() only proves the
        TCP connection is up; each provide() registers with this router
        over a separate, asynchronous $/register round trip, so this is
        the actual readiness signal a test needs before it can trust that
        a message for one of these methods will be routed anywhere."""
        import time as _time

        deadline = _time.monotonic() + timeout
        remaining = set(method_names)
        while remaining and _time.monotonic() < deadline:
            with self._registry_lock:
                remaining -= set(self._registry)
            if remaining:
                _time.sleep(0.02)
        return not remaining

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    # ---------------------------------------------------------------- I/O
    def _serve_connection(self, sock: socket.socket) -> None:
        conn = _Conn(sock)
        unpacker = msgpack.Unpacker(raw=False)
        try:
            while True:
                data = sock.recv(4096)
                if not data:
                    break
                unpacker.feed(data)
                for msg in unpacker:
                    self._dispatch(conn, msg)
        except (ConnectionError, OSError):
            pass
        finally:
            with self._registry_lock:
                for name in conn.registered:
                    if self._registry.get(name) is conn:
                        del self._registry[name]

    # ---------------------------------------------------------------- routing
    def _dispatch(self, conn: _Conn, msg: list) -> None:
        mtype = msg[0]
        if mtype == NOTIFICATION:
            _, method, params = msg
            if method == "$/register":  # notify form is not used by this client, but be lenient
                self._do_register(conn, params[0])
                return
            with self._registry_lock:
                target = self._registry.get(method)
            if target is not None:
                target.send([NOTIFICATION, method, params])
            else:
                log.debug("notification for unregistered method %r dropped", method)
            return
        if mtype == REQUEST:
            _, msgid, method, params = msg
            if method == "$/register":
                ok, err = self._do_register(conn, params[0])
                conn.send([RESPONSE, msgid, err, ok])
                return
            if method == "$/unregister":
                ok, err = self._do_unregister(conn, params[0])
                conn.send([RESPONSE, msgid, err, ok])
                return
            with self._registry_lock:
                target = self._registry.get(method)
            if target is None:
                conn.send([RESPONSE, msgid, [METHOD_NOT_AVAILABLE_ERR, f"no provider for {method!r}"], None])
                return
            with self._pending_lock:
                router_id = self._next_router_id
                self._next_router_id += 1
                self._pending[router_id] = (conn, msgid)
            target.send([REQUEST, router_id, method, params])
            return
        if mtype == RESPONSE:
            _, router_id, error, result = msg
            with self._pending_lock:
                origin = self._pending.pop(router_id, None)
            if origin is not None:
                origin_conn, origin_msgid = origin
                origin_conn.send([RESPONSE, origin_msgid, error, result])
            return
        log.warning("unrecognised message type %r", mtype)

    def _do_register(self, conn: _Conn, name: str) -> tuple[bool, list | None]:
        with self._registry_lock:
            owner = self._registry.get(name)
            if owner is not None and owner is not conn:
                return False, [ROUTE_ALREADY_EXISTS_ERR, f"{name!r} already provided"]
            self._registry[name] = conn
            conn.registered.add(name)
            return True, None

    def _do_unregister(self, conn: _Conn, name: str) -> tuple[bool, list | None]:
        with self._registry_lock:
            if self._registry.get(name) is not conn:
                return False, [METHOD_NOT_AVAILABLE_ERR, f"{name!r} not provided by this connection"]
            del self._registry[name]
            conn.registered.discard(name)
            return True, None
