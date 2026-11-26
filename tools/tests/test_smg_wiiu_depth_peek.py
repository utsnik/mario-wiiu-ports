#!/usr/bin/env python3
"""Host checks for the Wii U FLOAT_R32 to GXPeekZ conversion."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    source = root / "tools/tests/smg_wiiu_depth_peek_host.cpp"
    renderer = (root / "platform/wiiu/gx2_renderer.cpp").read_text()
    peek_body = renderer[renderer.index("bool peekZNeedsSync()"):renderer.index("void pixMetricClear()")]
    for forbidden in ("GX2WaitTimeStamp", "gx2prod::wait", "sms_wiiu_pipe5_wait_resource_fence"):
        if forbidden in peek_body:
            raise AssertionError(f"blocking operation remains in GXPeekZ path: {forbidden}")
    depth_service = renderer[renderer.index("static void producerDepthPeekService"):renderer.index("static bool queueProducerDepthPeek")]
    depth_slots = renderer[renderer.index("static bool initProducerDepthPeekSlots"):renderer.index("static void producerDepthPeekService")]
    queue_body = renderer[renderer.index("static bool queueProducerDepthPeek"):renderer.index("static uint32_t readProducerDepthPeek")]
    for forbidden in ("sms_wiiu_pipe5_wait_resource_fence", "GX2WaitTimeStamp", "gx2prod::wait",
                      "initProducerDepthPeekSlots"):
        if forbidden in depth_service:
            raise AssertionError(f"blocking/allocation operation remains in depth service: {forbidden}")
    # Slot init may wait for the pipe5 fence because only the frame thread calls
    # it (queueProducerDepthPeek); the ordered service must never reach it.
    if "initProducerDepthPeekSlots" not in queue_body:
        raise AssertionError("depth slots must be initialized from the frame-thread queue path")
    if renderer.count("initProducerDepthPeekSlots(") != 2:
        raise AssertionError("initProducerDepthPeekSlots must have exactly one caller")
    if "requeueing" in queue_body or "s_producerDepthPeekRequeues" in renderer:
        raise AssertionError("depth timeout must clear abandoned requests, not blindly requeue them")
    if "depth peek request lost" in queue_body and "return false;" not in queue_body:
        raise AssertionError("lost depth requests must yield before submitting a fresh request")
    for needle in ("request.source != current", "current->surface.image != request.sourceImage",
                   "!s_producerDepthPeekReady"):
        if needle not in depth_service:
            raise AssertionError(f"depth service lacks transition guard: {needle}")
    with tempfile.TemporaryDirectory() as temp:
        binary = Path(temp) / "depth-peek"
        subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
             str(source), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("depth-peek: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
