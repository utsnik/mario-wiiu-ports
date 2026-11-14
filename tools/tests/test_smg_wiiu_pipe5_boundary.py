#!/usr/bin/env python3
"""Check the stage-1 pipe5 service and VI boundaries without hardware."""

from pathlib import Path
import argparse
import re
import sys


def fail(message: str) -> str:
    return f"pipe5-boundary: FAIL: {message}"


def function_body(source: str, name: str):
    match = re.search(rf"\b{name}\s*\([^{{;]*\)\s*\{{", source)
    if not match:
        return None
    depth = 1
    index = match.end()
    while depth and index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    return source[match.end():index - 1] if depth == 0 else None


def block_body(source: str, marker: str):
    start = source.find(marker)
    if start < 0:
        return None
    opening = source.find("{", start)
    if opening < 0:
        return None
    depth = 1
    index = opening + 1
    while depth and index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    return source[opening + 1:index - 1] if depth == 0 else None


def body_at(source: str, opening: int):
    depth = 1
    index = opening + 1
    while depth and index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    return source[opening + 1:index - 1] if depth == 0 else None


def guarded_resource_sites(source: str):
    """Return the named free/replace sites and their source lines."""
    sites = (
        ("deferFree", "GX2DrawDone"),
        ("freeDeferred", "MEMFreeToDefaultHeap"),
        ("destroyShader", "shader.fetch.program"),
        ("initShader", "shader.fetch.program ="),
        ("initVertexProgram", "shader.fetchProgram ="),
        ("compactFetchFor", "void* program"),
        ("initTevVertex", "s_tevFetchProgram"),
        ("initTevPixel", "shader.psProgram ="),
        ("tevShaderForKey", "new TevShader"),
        ("vertexShaderForKey", "new VertexProgram"),
        ("tinyPsStub", "alignedZero"),
        ("releaseCopyImage", "MEMFree"),
        ("copyToTexture", "GX2DrawDone"),
        ("retileTexture", "deferFree(linear.image)"),
        ("textiledTextureService", "deferFree(linear.surface.image)"),
        ("textiledTexture", "e.textiledPending"),
        ("getTexture", "deferFree(e.texture.surface.image)"),
        ("packFreeLoading", "packLinear.surface.image"),
        ("packStart", "packLinear.surface.image"),
        ("packInstall", "e.texture = e.packTiled"),
        ("initProducerColourPeekSlots", "s_producerColourPeekSlots"),
        ("initProducerDepthPeekSlots", "s_producerDepthPeekSlots"),
        ("readFrameColourPixels", "s_peekColorLinear.image"),
        ("readTvPixels", "s_peekSurface.image"),
        ("peekZ", "s_tiled.image"),
        ("dynresFrameStart", "dynresResize"),
    )
    found = []
    errors = []
    for name, needle in sites:
        matches = list(re.finditer(rf"\b{name}\s*\([^{{;]*\)\s*\{{", source))
        matching_body = None
        matching_line = None
        for match in matches:
            body = body_at(source, source.find("{", match.start(), match.end()))
            if body is not None and needle in body:
                matching_body = body
                matching_line = source.count("\n", 0, match.start()) + 1
                break
        if matching_body is None:
            errors.append(f"cannot locate resource site {name} containing {needle}")
            continue
        if "sms_wiiu_pipe5_wait_resource_fence" not in matching_body:
            errors.append(f"resource site {name} lacks pipe5 fence wait")
            continue
        found.append(f"platform/wiiu/gx2_renderer.cpp:{matching_line} ({name})")
    return found, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--stub", type=Path, required=True)
    parser.add_argument("--vi", type=Path, required=True)
    parser.add_argument("--main-loop", type=Path, required=True)
    parser.add_argument("--game-system", type=Path, required=True)
    parser.add_argument("--gphase", type=Path, required=True)
    args = parser.parse_args()

    paths = (args.renderer, args.stub, args.vi, args.main_loop, args.game_system, args.gphase)
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        print(fail("missing input: " + ", ".join(missing)))
        return 1

    renderer = args.renderer.read_text(encoding="utf-8")
    stub = args.stub.read_text(encoding="utf-8")
    vi = args.vi.read_text(encoding="utf-8")
    main_loop = args.main_loop.read_text(encoding="utf-8")
    game_system = args.game_system.read_text(encoding="utf-8")
    gphase = args.gphase.read_text(encoding="utf-8")
    errors = []

    call_list = re.search(
        r"static void gx2ProdCallList\(void\* list, uint32_t bytes\)\s*\{(?P<body>.*?)\n\}",
        renderer,
        re.DOTALL,
    )
    if not call_list:
        errors.append("cannot locate gx2ProdCallList")
    else:
        body = re.sub(r"//[^\n]*", "", call_list.group("body"))
        forbidden = ("GXDrawDone", "GX2Flush", "GX2SwapScanBuffers", "VIFlush",
                     "prepareCopyDisp", "VIWaitForRetrace", "GX2WaitForVsync")
        for symbol in forbidden:
            if symbol in body:
                errors.append(f"category replay calls {symbol}")

    gate = re.search(
        r"int sms_wiiu_pipe5_enabled\(void\)\s*\{(?P<body>.*?)\n\}", renderer, re.DOTALL
    )
    if not gate:
        errors.append("cannot locate pipe5 gate")
    else:
        body = gate.group("body")
        for text in ("s_pipe5Marker", "!s_pipe2Marker", "s_gx2ProdEnabled"):
            if text not in body:
                errors.append(f"pipe5 gate lacks {text}")

    resource_sites, resource_errors = guarded_resource_sites(renderer)
    errors.extend(resource_errors)
    if "MEMAllocFromDefaultHeapEx" not in renderer or "sms_wiiu_pipe5_wait_resource_fence()" not in renderer:
        errors.append("renderer allocator boundary lacks the pipe5 resource fence")
    if "MEMFreeToDefaultHeap" not in renderer or "MEMFreeToExpHeap" not in renderer:
        errors.append("renderer free boundary lacks both heap guards")

    fence_wait = function_body(renderer, "sms_wiiu_pipe5_wait_resource_fence")
    if fence_wait is None:
        errors.append("cannot locate pipe5 resource-fence wait")
    else:
        if "s_pipe5DrainRuns" in fence_wait or "s_pipe5FrameStarted" in fence_wait:
            errors.append("resource-fence wait mutates renderer-frame drain ownership")

    compact = function_body(renderer, "compactFetchFor")
    if compact is None:
        errors.append("cannot locate compact fetch cache")
    else:
        lookup = compact.find("s_compactFetchCount;")
        wait = compact.find("sms_wiiu_pipe5_wait_resource_fence();")
        create = compact.find("alignedZero(GX2_SHADER_PROGRAM_ALIGNMENT")
        if lookup < 0 or wait < 0 or create < 0 or not (lookup < wait < create):
            errors.append("compact fetch fence wait is not creation-only")

    vertex = function_body(renderer, "vertexShaderForKey")
    if vertex is None:
        errors.append("cannot locate vertex shader cache")
    else:
        lookup = vertex.find("s_vertexProgramIndex.find")
        wait = vertex.find("sms_wiiu_pipe5_wait_resource_fence();")
        create = vertex.find("new VertexProgram")
        if lookup < 0 or wait < 0 or create < 0 or not (lookup < wait < create):
            errors.append("vertex shader fence wait is not creation-only")

    retire = function_body(renderer, "retireProducerFrame")
    if retire is None:
        errors.append("cannot locate producer-frame retirement")
    else:
        if "if (sms_wiiu_pipe5_enabled() && s_pipe5FrameStarted)" not in retire:
            errors.append("producer retirement publishes a pipe5 drain outside an active renderer frame")
        elif "++s_pipe5DrainRuns;" not in retire:
            errors.append("producer retirement does not count every pipe5 drain")

    renderer_begin = function_body(renderer, "sms_wiiu_gx2_renderer_begin")
    if renderer_begin is None:
        errors.append("cannot locate renderer begin")
    else:
        skipped = block_body(renderer_begin, "if (!s_ready || !ProcUIInForeground())")
        if skipped is None:
            errors.append("renderer begin skip path is missing")
        else:
            for text in ("s_pipe5FrameStarted = false;", "s_pipe5DrainRuns = 0;", "return;"):
                if text not in skipped:
                    errors.append(f"renderer begin skip path lacks {text}")

    pipe5_renderer_end = function_body(renderer, "sms_wiiu_gx2_renderer_pipe5_end_frame")
    if pipe5_renderer_end is None:
        errors.append("cannot locate renderer pipe5 end-frame handoff")
    else:
        if "s_pipe5FrameStarted = false;" not in pipe5_renderer_end:
            errors.append("pipe5 end-frame handoff leaves renderer ownership active")

    # Host/static regression for the observed sequence: a prior producer
    # retirement leaves drain=1, the next frame's renderer begin is skipped,
    # and frameLoop still reaches endFrame.  The skip must abandon the old
    # renderer ownership before endFrame can inspect the drain count.
    if renderer_begin is not None and pipe5_renderer_end is not None:
        skip_clear = all(text in renderer_begin for text in (
            "if (!s_ready || !ProcUIInForeground())",
            "s_pipe5FrameStarted = false;",
            "s_pipe5DrainRuns = 0;",
        ))
        end_checks_active = pipe5_renderer_end.find("if (!s_pipe5FrameStarted)")
        end_checks_drain = pipe5_renderer_end.find("if (s_pipe5DrainRuns)")
        if not skip_clear or end_checks_active < 0 or end_checks_drain < 0:
            errors.append("frameLoop skip -> stale producer drain sequence is not rejected")

    end_frame = function_body(stub, "sms_wiiu_pipe5_end_frame")
    if end_frame is None:
        errors.append("cannot locate pipe5 end-frame state machine")
    else:
        gate_pos = end_frame.find("sms_wiiu_pipe5_scene_ready()")
        fallback_pos = end_frame.find("!sceneReady")
        ended_twice = block_body(end_frame, "if (s_pipe5FrameEnded)")
        if ended_twice is None:
            errors.append("all pipe5 frames lack the ended-twice check")
        else:
            for text in (
                'WHBLogPrintf("SMS Wii U: [pipe5] game frame ended twice")',
                "s_pipe5Fallback = true;",
                "return SmsWiiuPipe5Fallback;",
            ):
                if text not in ended_twice:
                    errors.append(f"ended-twice path lacks {text}")
        if "if (!rendered)" in end_frame or "if (bufferMode < 2)" in end_frame:
            errors.append("end-frame ownership is gated away for some frames")
        state_set = end_frame.find("s_pipe5FrameEnded = true;")
        renderer_call = end_frame.find(
            "sms_wiiu_gx2_renderer_pipe5_end_frame", state_set if state_set >= 0 else 0
        )
        if gate_pos < 0 or fallback_pos < 0 or (renderer_call >= 0 and gate_pos > renderer_call):
            errors.append("end-frame gate is not checked before the renderer ownership handoff")
        if "smg_gphase_note_pipe5_gate_block();" not in end_frame:
            errors.append("end-frame gate does not count blocked frames")
        if renderer_call < 0:
            errors.append("pipe5 end-frame does not call the renderer handoff")
        if state_set < 0 or (renderer_call >= 0 and state_set > renderer_call):
            errors.append("pipe5 frames do not publish end-frame ownership before drain")

    consume = function_body(stub, "sms_wiiu_pipe5_consume_frame")
    if consume is None:
        errors.append("cannot locate pipe5 consume-frame state machine")
    else:
        for text in ("!s_pipe5FrameEnded", "!s_gfx_initialized", "!ProcUIInForeground"):
            if text not in consume:
                errors.append(f"consume-frame guard lacks {text}")
        for text in (
            "s_pipe5FrameEnded = false;",
            "s_pipe5Presented = false;",
            "s_pipe5Fallback = false;",
            "sms_wiiu_gx2_target_present();",
            "present_finish_pipelined(\"fallback\")",
            "present_begin_pipelined();",
            "sms_wiiu_gx2_texture_pack_pump();",
            "sms_wiiu_gx2_target_begin();",
            "sms_wiiu_gx2_renderer_begin();",
            "s_frame_open = true;",
            "sms_wiiu_pipe5_trace(P5T_CONSUME",
        ):
            if text not in consume:
                errors.append(f"consume-frame lacks {text}")

    gfx_frame = function_body(stub, "sms_wiiu_gfx_frame")
    if gfx_frame is None:
        errors.append("cannot locate gfx-frame state machine")
    else:
        pipe5_branch = block_body(gfx_frame, "if (sms_wiiu_pipe5_enabled())")
        if pipe5_branch is None:
            errors.append("gfx-frame lacks its pipe5 branch")
        else:
            for text in (
                "sms_wiiu_gx2_renderer_begin",
                "present_begin_pipelined",
                "WHBGfxBeginRender",
            ):
                if text in pipe5_branch:
                    errors.append(f"gfx-frame pipe5 branch still contains {text}")
            for text in (
                "s_pipe5FrameEnded = false;",
                "s_pipe5Presented = false;",
                "s_pipe5Fallback = false;",
            ):
                if text in pipe5_branch:
                    errors.append(f"gfx-frame pipe5 branch still consumes {text}")

    trace_enum = re.search(r"enum Pipe5TraceEvent\s*\{(?P<body>.*?)\};", stub, re.DOTALL)
    if not trace_enum or "P5T_CONSUME" not in trace_enum.group("body"):
        errors.append("pipe5 trace enum lacks the consume event")
    if '"consume"' not in stub:
        errors.append("pipe5 trace names lack consume")

    service = function_body(stub, "sms_wiiu_pipe5_service_present_complete")
    if service is None or "s_pipe5FrameEnded" not in service:
        errors.append("present completion is not tied to the active rendered frame")

    fields = vi.find("const u32 fields = sms_wiiu_vsync_fields();")
    after_fields = vi.find("if (!s_presentFirst)\n\t\tsms_wiiu_gfx_frame();", fields)  # pipe5 must reach gfx_frame here (b224)
    before_fields = vi.find("if (sms_wiiu_pipe5_enabled())", 0, fields)
    if fields < 0 or after_fields < 0 or before_fields >= 0:
        errors.append("pipe5 graphics handoff is not exclusively after vsync_fields")

    if "SmsWiiuPipe5OverlapOwned" not in main_loop or "if (!pipe5OwnsEndFrame)" not in main_loop:
        errors.append("main loop does not guard the legacy double/triple end-frame wait")
    tick = function_body(main_loop, "waitForTick")
    if tick is None or "OSReceiveMessage" not in tick or "VIWaitForRetrace();" not in tick:
        errors.append("waitForTick no longer documents the queue/retrace boundary")
    if "sms_wiiu_frame_boundary();" not in vi or "s_post_callback" not in vi:
        errors.append("VI boundary does not reach the post-retrace callback")
    if "sms_wiiu_pipe5_service_present_complete" not in stub:
        errors.append("present completion is not recorded at the service boundary")

    frame_loop = function_body(game_system, "GameSystem::frameLoop")
    if frame_loop is None:
        errors.append("cannot locate GameSystem::frameLoop")
    else:
        call_pattern = r"\bsms_wiiu_pipe5_consume_frame\s*\(\s*\)\s*;"
        call_sites = re.findall(call_pattern, game_system)
        if len(call_sites) != 1:
            errors.append("pipe5 consume-frame does not have exactly one call site")
        consume_pos = frame_loop.find("sms_wiiu_pipe5_consume_frame();")
        begin_pos = frame_loop.find("MainLoopFramework::sManager->beginRender();")
        if consume_pos < 0:
            errors.append("frameLoop does not consume the pipe5 boundary")
        elif begin_pos < 0 or consume_pos > begin_pos:
            errors.append("frameLoop consumes pipe5 after beginRender")
        if consume_pos >= 0 and "smg_gphase_begin_frame();" in frame_loop:
            if consume_pos > frame_loop.find("smg_gphase_begin_frame();"):
                errors.append("frameLoop consume is not at the top")
        reorder_loop = block_body(frame_loop, "if (smg_reorder_enabled())")
        ordered = (
            "MainLoopFramework::sManager->beginRender();",
            "draw();",
            "update();",
            "calcAnim();",
        )
        positions = [reorder_loop.find(text) for text in ordered] if reorder_loop else []
        end_frame_pos = frame_loop.find("MainLoopFramework::sManager->endFrame();")
        wait_retrace_pos = frame_loop.find("MainLoopFramework::sManager->waitForRetrace();", end_frame_pos)
        if (reorder_loop is None or any(position < 0 for position in positions) or
                positions != sorted(positions) or end_frame_pos < 0 or
                wait_retrace_pos < 0 or wait_retrace_pos < end_frame_pos):
            errors.append("frameLoop does not preserve consume/begin/draw/update/calc/endFrame/retrace order")
        for source, name in ((stub, "stub"), (vi, "vi"), (main_loop, "main-loop")):
            if re.search(call_pattern, source):
                errors.append(f"pipe5 consume has an unexpected {name} call site")
        scene_frame_pos = frame_loop.find("sms_wiiu_pipe5_scene_frame();")
        if scene_frame_pos < 0 or (consume_pos >= 0 and scene_frame_pos < consume_pos):
            errors.append("frameLoop does not advance the scene warm-up after consume")
        if scene_frame_pos >= 0 and scene_frame_pos > frame_loop.find("smg_gphase_begin_frame();"):
            errors.append("scene warm-up is not sampled at the top of frameLoop")

    for text in (
        "kPipe5SceneWarmupFrames = 120",
        "isChangingScene()",
        "s_pipe5SceneFrames >= kPipe5SceneWarmupFrames",
    ):
        if text not in game_system:
            errors.append(f"scene gate lacks {text}")
    for text in (
        "smg_gphase_note_pipe5_gate_block",
        "smg_gphase_note_pipe5_resource_fence",
        "s_pipe5_gate_blocks",
        "s_pipe5_resource_fence_waits",
        "p5gate=",
        "p5resourceFence=",
    ):
        if text not in gphase:
            errors.append(f"gphase lacks {text}")

    if errors:
        for error in errors:
            print(fail(error))
        return 1
    print("pipe5-boundary: PASS: category replay, scene gate, and resource fences are safe")
    print("pipe5-boundary: guarded-sites: " + ", ".join(resource_sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
