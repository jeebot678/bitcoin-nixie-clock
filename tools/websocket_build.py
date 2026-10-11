"""Bound only the WebSocket client's TLS constructor using build middleware."""
from pathlib import Path

Import("env")


def bounded_tls(build_env, node):
    header = Path(build_env.subst("$PROJECT_DIR")) / "include/BoundedWebSocketTls.h"
    return build_env.Object(node, CXXFLAGS=build_env["CXXFLAGS"] + ["-include", str(header)])


env.AddBuildMiddleware(bounded_tls, "*WebSocketsClient.cpp")
