"""Extract bounded, evidence-backed features from an oracle trace."""
import argparse
import json
import pathlib
import sys

repo_root = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(repo_root / "consoles/md/tests/oracle"))
from trace import Event, M68K, Z80, VDP, YM2612, READ, WRITE, DMA, IRQ, read as read_trace

DMA_FILL_ADDR_TAG = 0x10000000
DMA_COPY_ADDR_TAG = 0x20000000


class FeatureError(ValueError):
    """The trace or run manifest is incomplete or unsupported."""


def _event_record(event):
    return {
        "clock": event.clock,
        "device": event.device,
        "operation": event.operation,
        "width": event.width,
        "address": event.address,
        "value": event.value,
    }


def _observation(capabilities, required, evidence, value=None, scope_reason=None):
    missing = sorted(set(required) - capabilities)
    result = {"evidence": evidence}
    if value is not None:
        result["value"] = value
    if missing:
        result["status"] = "unavailable"
        result["reason"] = "missing trace capabilities: " + ", ".join(missing)
    elif scope_reason:
        result["status"] = "unavailable"
        result["reason"] = scope_reason
    elif evidence:
        result["status"] = "observed"
    else:
        result["status"] = "not_observed"
        result["reason"] = "no matching event in this bounded run"
    return result


def _register_writes(events):
    writes = []
    for event in events:
        if (event.device == VDP and event.operation == WRITE and event.address == 4 and
                event.width == 2 and event.value & 0xC000 == 0x8000):
            writes.append({
                "clock": event.clock,
                "register": event.value >> 8 & 0x1F,
                "value": event.value & 0xFF,
            })
    return writes


def _last_register(writes, register):
    matches = [write for write in writes if write["register"] == register]
    return matches[-1] if matches else None


def _sram_range(manifest):
    window = manifest.get("sram_window")
    if not isinstance(window, dict):
        return None
    start, end = window.get("start"), window.get("end")
    if (not isinstance(start, int) or isinstance(start, bool) or
            not isinstance(end, int) or isinstance(end, bool) or
            not 0 <= start < end <= 0x1000000):
        raise FeatureError("invalid SRAM window metadata")
    return start, end


def _scope_reason(manifest, scope_key, required_range):
    scope = manifest.get("trace_scope")
    if scope is None or scope == "full-candidate-trace":
        return None
    if scope != "survey-feature-filter-v1":
        return "unsupported trace scope: " + str(scope)
    filters = manifest.get("trace_filters")
    recorded = filters.get(scope_key, []) if isinstance(filters, dict) else []
    if "all" in recorded or required_range in recorded:
        return None
    return f"trace filter does not cover {scope_key}:{required_range}"


def _six_button_probe(events, capabilities, manifest):
    required = ["m68k_cpu_access"]
    scope_reason = _scope_reason(manifest, "m68k_cpu_access", "pad_ports")
    data_ports = (0xA10003, 0xA10005, 0xA10007)
    matched = []
    for port in data_ports:
        previous_th = None
        transitions = []
        for event in events:
            if event.device != M68K or event.address != port:
                continue
            if event.operation == WRITE:
                th = event.value & 0x40
                if previous_th is not None and th != previous_th:
                    transitions.append(event)
                    transitions = transitions[-3:]
                previous_th = th
            elif event.operation == READ and len(transitions) == 3:
                if event.clock - transitions[0].clock <= 1_000_000:
                    matched = [_event_record(item) for item in transitions]
                    matched.append(_event_record(event))
                    return _observation(capabilities, required, matched, {
                        "data_port": port,
                        "heuristic": "read after three TH transitions within 1,000,000 master clocks",
                    }, scope_reason)
    return _observation(capabilities, required, matched, {
        "heuristic": "read after three TH transitions within 1,000,000 master clocks",
    }, scope_reason)


def _z80_dac_writes(events):
    z80_data = [event for event in events if event.device == Z80 and event.operation == WRITE and
                event.address == 0x4001]
    ym_port_data = [event for event in events if event.device == YM2612 and event.operation == WRITE and
                    event.address == 1]
    ym_dac = [event for event in events if event.device == YM2612 and event.operation == WRITE and
              event.address == 0x2A]
    matches = []
    for register_event in ym_dac:
        paired = False
        for bus_event in z80_data:
            if bus_event.value != register_event.value or not 0 <= register_event.clock - bus_event.clock < 42:
                continue
            if any(port_event.value == bus_event.value and port_event.clock == bus_event.clock
                   for port_event in ym_port_data):
                matches.append({
                    "z80_bus": _event_record(bus_event),
                    "ym_port": _event_record(next(port_event for port_event in ym_port_data
                                                   if port_event.value == bus_event.value and
                                                   port_event.clock == bus_event.clock)),
                    "ym_register": _event_record(register_event),
                })
                paired = True
                break
        if not paired:
            continue
    return matches


def extract_features(manifest, events):
    """Return feature observations with explicit bounded-run coverage status.

    Each observation is ``observed``, ``not_observed`` under the supplied
    bounded trace, or ``unavailable`` when the trace lacks a required capability
    or the run lacks metadata needed to interpret an address range.
    """
    if not isinstance(manifest, dict):
        raise FeatureError("run manifest must be an object")
    frames = manifest.get("frames")
    trace_count = manifest.get("trace_events")
    capability_names = manifest.get("trace_capabilities")
    if (not isinstance(frames, int) or isinstance(frames, bool) or frames < 1 or
            not isinstance(trace_count, int) or isinstance(trace_count, bool) or trace_count < 0 or
            not isinstance(capability_names, list) or
            any(not isinstance(name, str) for name in capability_names)):
        raise FeatureError("run manifest lacks valid bounded trace metadata")
    events = list(events)
    if len(events) != trace_count or any(not isinstance(event, Event) for event in events):
        raise FeatureError("trace event count or record type does not match manifest")
    capabilities = set(capability_names)
    register_writes = _register_writes(events)

    def register_feature(register, mask, value_name):
        write = _last_register(register_writes, register)
        evidence = [] if write is None else [write]
        value = None if write is None else {value_name: bool(write["value"] & mask),
                                            "register_value": write["value"]}
        return _observation(capabilities, vdp_cap, evidence, value)

    mode4_write = _last_register(register_writes, 0)
    mode5_write = _last_register(register_writes, 1)
    interlace_write = _last_register(register_writes, 12)
    interlace_value = None if interlace_write is None else {
        "enabled": bool(interlace_write["value"] & 0x02),
        "double_resolution": bool(interlace_write["value"] & 0x02 and interlace_write["value"] & 0x04),
        "register_value": interlace_write["value"],
    }

    vdp_register_evidence = register_writes
    vdp_cap = ["vdp_port"]
    vdp_scope_reason = _scope_reason(manifest, "device_events", "vdp")
    mode_evidence = [write for write in (mode4_write, mode5_write) if write is not None]
    if vdp_scope_reason:
        mode_observation = {"status": "unavailable", "evidence": mode_evidence,
                            "reason": vdp_scope_reason}
    elif mode4_write is None and mode5_write is None:
        mode_observation = {"status": "not_observed", "evidence": [],
                            "reason": "mode registers were not written in this bounded run"}
    elif mode4_write is None or mode5_write is None:
        mode_observation = {"status": "unavailable", "evidence": mode_evidence,
                            "reason": "both mode registers are needed to classify the active VDP mode"}
    else:
        mode4_enabled = bool(mode4_write["value"] & 0x04)
        mode5_enabled = bool(mode5_write["value"] & 0x04)
        mode = "mode5" if mode4_enabled and mode5_enabled else "mode4" if mode4_enabled else "inactive"
        mode_observation = {"status": "observed", "evidence": mode_evidence,
                            "value": {"mode": mode, "mode4_bit": mode4_enabled,
                                      "mode5_bit": mode5_enabled,
                                      "register0": mode4_write["value"],
                                      "register1": mode5_write["value"]}}

    features = {
        "vdp_registers": _observation(capabilities, vdp_cap, vdp_register_evidence,
                                       scope_reason=vdp_scope_reason),
        "vdp_mode": mode_observation,
        "interlace": _observation(capabilities, vdp_cap,
                                   [] if interlace_write is None else [interlace_write], interlace_value,
                                   vdp_scope_reason),
        "shadow_highlight": _observation(
            capabilities, vdp_cap,
            [] if interlace_write is None else [interlace_write],
            None if interlace_write is None else {"enabled": bool(interlace_write["value"] & 0x08),
                                                  "register_value": interlace_write["value"]},
            vdp_scope_reason),
        "hint_enabled": _observation(
            capabilities, vdp_cap,
            [] if mode4_write is None else [mode4_write],
            None if mode4_write is None else {"enabled": bool(mode4_write["value"] & 0x10),
                                              "register_value": mode4_write["value"]},
            vdp_scope_reason),
    }

    for name, capability in (("dma_68k_to_vdp", "vdp_68k_dma_source"),
                             ("dma_fill", "vdp_dma_fill"),
                             ("dma_copy", "vdp_dma_copy")):
        dma_events = [event for event in events if event.device == VDP and event.operation == DMA]
        if capability == "vdp_68k_dma_source":
            dma_events = [event for event in dma_events if event.width == 2 and
                          event.address < DMA_FILL_ADDR_TAG]
        elif capability == "vdp_dma_fill":
            dma_events = [event for event in dma_events if event.width == 1 and
                          event.address & 0xF0000000 == DMA_FILL_ADDR_TAG]
        elif capability == "vdp_dma_copy":
            dma_events = [event for event in dma_events if event.width == 1 and
                          event.address & 0xF0000000 == DMA_COPY_ADDR_TAG]
        features[name] = _observation(capabilities, [capability],
                                      [_event_record(event) for event in dma_events],
                                      scope_reason=_scope_reason(manifest, "device_events", "dma"))

    hint_events = [event for event in events if event.device == VDP and event.operation == IRQ]
    features["hint_lines"] = _observation(
        capabilities, ["vdp_hint_lines"], [_event_record(event) for event in hint_events],
        {"line_count": len(hint_events)}, _scope_reason(manifest, "device_events", "hint"))

    dac_matches = _z80_dac_writes(events)
    features["z80_dac_streaming"] = _observation(
        capabilities, ["z80_cpu_access", "ym2612_write", "ym2612_port_write"],
        dac_matches if len(dac_matches) >= 2 else [], {"matched_writes": len(dac_matches)},
        _scope_reason(manifest, "z80_cpu_access", "ym_ports"))

    direct_ym = [event for event in events if event.device == M68K and event.operation == WRITE and
                 0xA04000 <= event.address < 0xA06000]
    features["direct_68k_ym_writes"] = _observation(
        capabilities, ["m68k_cpu_access", "ym2612_port_write"],
        [_event_record(event) for event in direct_ym],
        scope_reason=_scope_reason(manifest, "m68k_cpu_access", "ym_ports"))

    features["six_button_probe"] = _six_button_probe(events, capabilities, manifest)

    mapper_writes = [event for event in events if event.device == M68K and event.operation == WRITE and
                     0xA13000 <= event.address < 0xA13100]
    features["mapper_register_access"] = _observation(
        capabilities, ["m68k_cpu_access"], [_event_record(event) for event in mapper_writes],
        scope_reason=_scope_reason(manifest, "m68k_cpu_access", "mapper_registers"))

    sram_range = _sram_range(manifest)
    if sram_range is None:
        features["sram_access"] = {
            "status": "unavailable", "evidence": [],
            "reason": "cartridge SRAM window metadata was not supplied",
        }
    else:
        start, end = sram_range
        sram_events = [event for event in events if event.device == M68K and event.operation in (READ, WRITE) and
                       start <= event.address < end]
        features["sram_access"] = _observation(
            capabilities, ["m68k_cpu_access"], [_event_record(event) for event in sram_events],
            {"start": start, "end": end},
            _scope_reason(manifest, "m68k_cpu_access", "sram_window"))

    return {
        "core_sha256": manifest.get("core_sha256"),
        "frames": frames,
        "input": manifest.get("input", {}),
        "trace_capabilities": sorted(capabilities),
        "trace_scope": manifest.get("trace_scope", "full-candidate-trace"),
        "features": features,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--trace", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, help="write the feature JSON here instead of stdout")
    args = parser.parse_args()
    with args.manifest.open() as stream:
        manifest = json.load(stream)
    with args.trace.open("rb") as stream:
        events = list(read_trace(stream))
    result = json.dumps(extract_features(manifest, events), indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.write_text(result)
    else:
        sys.stdout.write(result)


if __name__ == "__main__":
    try:
        main()
    except (OSError, json.JSONDecodeError, FeatureError) as exc:
        sys.exit(f"features: {exc}")
