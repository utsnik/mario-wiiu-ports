#!/usr/bin/env python3
"""Host contract test for the SMG JPA async live-job boundary."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("smg_wiiu_jpa_async_host.cpp")


def source_contracts() -> None:
    header = (ROOT / "games/smg/include/wiiu/jpa_async.h").read_text(encoding="utf-8")
    final_vx = (ROOT / "platform/wiiu/jpa_final_vertex.h").read_text(encoding="utf-8")
    renderer = (ROOT / "platform/wiiu/gx2_renderer.cpp").read_text(encoding="utf-8")
    jpa = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAResource.cpp").read_text(encoding="utf-8")
    helper = (ROOT / "games/smg/platform/wiiu/jpaasync.cpp").read_text(encoding="utf-8")
    worker = (ROOT / "platform/wiiu/gx2_producer_worker.cpp").read_text(encoding="utf-8")
    counters = (ROOT / "platform/wiiu/profiler.cpp").read_text(encoding="utf-8")
    manager = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAEmitterManager.cpp").read_text(encoding="utf-8")
    emitter = (ROOT / "games/smg/decomp/src/JSystem/JParticle/JPAEmitter.cpp").read_text(encoding="utf-8")
    effect = (ROOT / "games/smg/decomp/src/Game/Effect/EffectSystem.cpp").read_text(encoding="utf-8")
    holder = (ROOT / "games/smg/decomp/src/Game/Effect/ParticleEmitterHolder.cpp").read_text(encoding="utf-8")
    gxstub = (ROOT / "platform/wiiu/gx_stub.cpp").read_text(encoding="utf-8")
    internal = (ROOT / "platform/gx/src/gx_internal.h").read_text(encoding="utf-8")
    fifo = (ROOT / "platform/gx/src/gx_fifo.cpp").read_text(encoding="utf-8")
    assert "struct Job" in header
    assert "JPAEmitterWorkData work" in header
    assert "JPANode<JPABaseParticle>* first" in header
    assert "JPANode<JPABaseParticle>* end" in header
    assert "bool reverse" in header
    assert "struct Snapshot" not in header and "snapshot->" not in jpa
    assert "billboardMatrix" not in header
    assert "publishReady" in header and "workerCanRead" in header
    assert "SMS_WIIU_JPA_FINALVX_PENDING" in final_vx
    assert "volatile uint32_t ready" in final_vx
    assert 's_jpaAsyncMarker = sdMarkerPresent("jpaasync")' in renderer
    assert 'sdGameDefaultMarker("jpaasync")' not in renderer
    assert "finalVxPayloadForRun" in jpa and "finalVxBillboardPayloadForRun" in jpa
    for needle in ("OSCreateThread", "1 << 2", "s_published", "QUEUE_FULL",
                   "sms_wiiu_jpa_async_join", "sms_wiiu_jpa_finalvx_publish_ready"):
        assert needle in helper, needle
    assert "GX" not in helper
    assert "DCFlushRange(descriptor, sizeof(*descriptor))" in helper
    for needle in ("waitJpaFinalVxReady", "OSYieldThread",
                   "sms_wiiu_jpa_async_worker_wait"):
        assert needle in worker, needle
    # The worker invalidates the descriptor/payload before reading it, and the
    # counters are monotonic (a reset raced a worker-side join).
    wait_body = worker.split("bool waitJpaFinalVxReady", 1)[1].split("\n}\n", 1)[0]
    assert "DCInvalidateRange" in wait_body
    for reset in ("s_published = 0", "s_claim = 0", "s_completed = 0"):
        assert reset not in helper, reset
    assert "% kMaxJobs" in helper
    for needle in ("s_jpaAsyncJobs", "s_jpaAsyncParticles", "helper-busy-us", "worker-wait-us",
                   "sms_wiiu_jpa_async_report"):
        assert needle in counters, needle
    assert "waitJpaFinalVxReady" in renderer
    assert "sms_wiiu_jpa_async_join();" in manager
    assert "sms_wiiu_jpa_async_join();" in emitter
    assert "sms_wiiu_jpa_async_join();" in effect
    assert holder.count("sms_wiiu_jpa_async_join();") >= 2
    assert "sms_wiiu_jpa_async_join();" in gxstub
    assert "ready flag" in internal and "only the descriptor address" in fifo
    assert "descriptor address" in fifo and "jpaFinalVxSubmit" in fifo
    for needle in ("finalVxAsyncBillboardPayloadForRun", "finalVxAsyncDirectionalPayloadForRun",
                   "sms_wiiu_jpa_async_enqueue", "fillAsyncBillboard", "fillAsyncDirectional",
                   "job.work = *work", "job.first = first", "job.end = runEnd"):
        assert needle in jpa, needle
    billboard_fill = jpa[jpa.index("static void fillAsyncBillboard"):jpa.index("static void fillAsyncDirectional")]
    directional_fill = jpa[jpa.index("static void fillAsyncDirectional"):jpa.index("static void fillAsyncFinalVx")]
    for body in (billboard_fill, directional_fill):
        assert "sms_wiiu_jpa_finalvx_alloc" not in body
        assert "GXSet" not in body and "GXBegin" not in body and "GXPosition" not in body


def main() -> int:
    source_contracts()
    with tempfile.TemporaryDirectory(prefix="smg-jpa-async-") as directory:
        executable = Path(directory) / "jpa_async_host"
        build = subprocess.run(
            ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
             str(SOURCE), "-o", str(executable)], capture_output=True, text=True
        )
        if build.returncode:
            print("jpa-async: FAIL (compile)\n" + build.stderr)
            return 1
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        print((run.stdout + run.stderr).strip())
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
