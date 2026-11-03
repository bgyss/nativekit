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
YM_PORT_M68K_ADDR_TAG = 0x80000000
YM_PORT_Z80_ADDR_TAG = 0x40000000
YM_PORT_SOURCE_MASK = 0xF0000000
TH_TIMEOUT = 56000
MAX_HINT_LINES = 512


class FeatureError(ValueError):
    """The trace or run manifest is incomplete or unsupported."""


def _event_record(event, include_value=True):
    record = {
        "clock": event.clock,
        "device": event.device,
        "operation": event.operation,
        "width": event.width,
        "address": event.address,
    }
    if include_value:
        record["value"] = event.value
    return record


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
    mode5_enabled = False
    pending_second_word = False
    for event in events:
        if (pending_second_word and event.device == VDP and event.operation in (READ, WRITE) and
                event.address in (0, 4) and
                not (event.operation == WRITE and event.address == 4 and event.width == 2)):
            pending_second_word = False
        if (event.device != VDP or event.operation != WRITE or event.address != 4 or
                event.width != 2):
            continue
        if pending_second_word:
            pending_second_word = False
            continue
        if event.value & 0xC000 == 0x8000:
            register = event.value >> 8 & 0x1F
            value = event.value & 0xFF
            writes.append({"clock": event.clock, "register": register, "value": value})
            if register == 1:
                mode5_enabled = bool(value & 0x04)
        elif mode5_enabled:
            pending_second_word = True
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
    feature_only = manifest.get("capture_mode") == "feature-only"
    if scope is None:
        return "feature-only trace is missing explicit scope metadata" if feature_only else None
    if scope == "full-candidate-trace":
        if feature_only:
            return "feature-only trace cannot claim full-candidate scope"
        return None
    if scope != "survey-feature-filter-v1":
        return "unsupported trace scope: " + str(scope)
    filters = manifest.get("trace_filters")
    if not isinstance(filters, dict):
        return "invalid trace filter metadata"
    recorded = filters.get(scope_key, [])
    if (not isinstance(recorded, list) or
            any(not isinstance(item, str) for item in recorded)):
        return "invalid trace filter metadata"
    if "all" in recorded or required_range in recorded:
        return None
    return f"trace filter does not cover {scope_key}:{required_range}"


def _six_button_probe(events, capabilities, manifest):
    required = ["m68k_cpu_access"]
    scope_reason = _scope_reason(manifest, "m68k_cpu_access", "pad_ports")
    data_ports = (0xA10003, 0xA10005, 0xA10007)
    for port in data_ports:
        port_index = (port - data_ports[0]) // 2
        control_port = 0xA10009 + port_index * 2
        control = 0
        data = 0
        known_data = False
        th_high = None
        rise_count = 0
        timeout_cycle = None
        rises = []
        for event in events:
            if event.device != M68K:
                continue
            if event.width == 1:
                io_port = event.address
            elif event.width == 2 and event.address & 1 == 0:
                io_port = event.address + 1
            else:
                continue
            if io_port not in (port, control_port):
                continue
            value = event.value & 0xFF
            if io_port == control_port and event.operation == WRITE:
                old_direction = bool(control & 0x40)
                control = value
                new_direction = bool(control & 0x40)
                if new_direction != old_direction:
                    rise_count = 0
                    rises = []
                    timeout_cycle = None
                    th_high = bool(known_data and new_direction and data & 0x40) if known_data else None
            elif io_port == port and event.operation == WRITE:
                was_known = known_data
                data = value
                known_data = True
                if not control & 0x40:
                    th_high = False
                    continue
                next_th_high = bool(data & 0x40)
                if not was_known or th_high is None:
                    th_high = next_th_high
                    continue
                if next_th_high != th_high:
                    if timeout_cycle is not None and event.clock >= timeout_cycle:
                        rise_count = 0
                        rises = []
                    if next_th_high:
                        rise_count += 1
                        rises.append(event)
                    timeout_cycle = event.clock + TH_TIMEOUT
                th_high = next_th_high
            elif io_port == port and event.operation == READ:
                if timeout_cycle is not None and event.clock >= timeout_cycle:
                    rise_count = 0
                    rises = []
                if th_high and rise_count == 3:
                    evidence = [_event_record(item) for item in rises[-3:]]
                    evidence.append(_event_record(event))
                    return _observation(capabilities, required, evidence, {
                        "data_port": port,
                        "heuristic": "read with TH high after three rising edges within 56,000 clocks",
                    }, scope_reason)
    return _observation(capabilities, required, [], {
        "heuristic": "read with TH high after three rising edges within 56,000 clocks",
    }, scope_reason)


def _z80_dac_writes(events):
    z80_data = [event for event in events if event.device == Z80 and event.operation == WRITE and
                event.address == 0x4001]
    ym_port_data = [event for event in events if event.device == YM2612 and event.operation == WRITE and
                    event.address & YM_PORT_SOURCE_MASK == YM_PORT_Z80_ADDR_TAG and
                    event.address & 3 == 1]
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
                    "z80_bus": _event_record(bus_event, include_value=False),
                    "ym_port": _event_record(next(port_event for port_event in ym_port_data
                                                   if port_event.value == bus_event.value and
                                                   port_event.clock == bus_event.clock),
                                              include_value=False),
                    "ym_register": _event_record(register_event, include_value=False),
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
        history = [write for write in register_writes if write["register"] == register]
        write = history[-1] if history else None
        evidence = [] if write is None else [write]
        value = None if write is None else {
            value_name: bool(write["value"] & mask),
            "ever_enabled": any(item["value"] & mask for item in history),
            "register_value": write["value"],
        }
        return _observation(capabilities, vdp_cap, evidence, value, vdp_scope_reason)

    mode4_write = _last_register(register_writes, 0)
    mode5_write = _last_register(register_writes, 1)
    interlace_write = _last_register(register_writes, 12)
    interlace_history = [write for write in register_writes if write["register"] == 12]
    interlace_value = None if interlace_write is None else {
        "enabled": bool(interlace_write["value"] & 0x02),
        "ever_enabled": any(write["value"] & 0x02 for write in interlace_history),
        "double_resolution": bool(interlace_write["value"] & 0x02 and interlace_write["value"] & 0x04),
        "register_value": interlace_write["value"],
    }

    last_register_writes = {}
    for write in register_writes:
        last_register_writes[write["register"]] = write
    register_ids = sorted(last_register_writes)
    vdp_register_evidence = [last_register_writes[register] for register in register_ids]
    vdp_register_value = {
        "register_ids": register_ids,
        "last_values": {str(register): last_register_writes[register]["value"]
                        for register in register_ids},
    }
    vdp_cap = ["vdp_port"]
    vdp_scope_reason = _scope_reason(manifest, "device_events", "vdp")
    mode_evidence = [write for write in (mode4_write, mode5_write) if write is not None]
    if vdp_scope_reason:
        mode_observation = {"status": "unavailable", "evidence": mode_evidence,
                            "reason": vdp_scope_reason}
    elif "vdp_port" not in capabilities:
        mode_observation = _observation(capabilities, vdp_cap, mode_evidence)
    elif mode4_write is None and mode5_write is None:
        mode_observation = {"status": "not_observed", "evidence": [],
                            "reason": "mode registers were not written in this bounded run"}
    elif mode4_write is None or mode5_write is None:
        mode_observation = {"status": "unavailable", "evidence": mode_evidence,
                            "reason": "both mode registers are needed to classify the active VDP mode"}
    else:
        mode4_enabled = bool(mode4_write["value"] & 0x04)
        mode5_enabled = bool(mode5_write["value"] & 0x04)
        mode = "mode5" if mode5_enabled else "mode4" if mode4_enabled else "inactive"
        mode_observation = {"status": "observed", "evidence": mode_evidence,
                            "value": {"mode": mode, "mode4_bit": mode4_enabled,
                                      "mode5_bit": mode5_enabled,
                                      "register0": mode4_write["value"],
                                      "register1": mode5_write["value"]}}

    features = {
        "vdp_registers": _observation(capabilities, vdp_cap, vdp_register_evidence,
                                       vdp_register_value, vdp_scope_reason),
        "vdp_mode": mode_observation,
        "interlace": _observation(capabilities, vdp_cap,
                                   [] if interlace_write is None else [interlace_write], interlace_value,
                                   vdp_scope_reason),
        "shadow_highlight": register_feature(12, 0x08, "enabled"),
        "hint_enabled": register_feature(0, 0x10, "enabled"),
    }

    for name, capability in (("dma_68k_to_vdp", "vdp_68k_dma_source"),
                             ("dma_fill", "vdp_dma_fill"),
                             ("dma_copy", "vdp_dma_copy")):
        dma_events = [event for event in events if event.device == VDP and event.operation == DMA]
        if capability == "vdp_68k_dma_source":
            dma_events = [event for event in dma_events if event.width == 2 and
                          event.address < DMA_FILL_ADDR_TAG]
        elif capability == "vdp_dma_fill":
            dma_events = [event for event in dma_events if event.width in (1, 2) and
                          event.address & 0xF0000000 == DMA_FILL_ADDR_TAG]
        elif capability == "vdp_dma_copy":
            dma_events = [event for event in dma_events if event.width == 1 and
                          event.address & 0xF0000000 == DMA_COPY_ADDR_TAG]
        features[name] = _observation(capabilities, [capability],
                                      [_event_record(event, include_value=False) for event in dma_events],
                                      scope_reason=_scope_reason(manifest, "device_events", "dma"))

    hint_events = [event for event in events if event.device == VDP and event.operation == IRQ]
    hint_lines = sorted({event.address for event in hint_events})[:MAX_HINT_LINES]
    hint_line_events = []
    seen_hint_lines = set()
    for event in hint_events:
        if event.address in seen_hint_lines:
            continue
        seen_hint_lines.add(event.address)
        hint_line_events.append(_event_record(event))
        if len(hint_line_events) == MAX_HINT_LINES:
            break
    features["hint_lines"] = _observation(
        capabilities, ["vdp_hint_lines"], hint_line_events,
        {"line_count": len(hint_lines), "event_count": len(hint_events), "lines": hint_lines},
        _scope_reason(manifest, "device_events", "hint"))

    dac_matches = _z80_dac_writes(events)
    features["z80_dac_streaming"] = _observation(
        capabilities, ["z80_cpu_access", "ym2612_write", "ym2612_port_write"],
        dac_matches if len(dac_matches) >= 2 else [], {"matched_writes": len(dac_matches)},
        _scope_reason(manifest, "z80_cpu_access", "ym_ports"))

    ym_port_writes = [event for event in events if event.device == YM2612 and
                      event.operation == WRITE and
                      event.address & YM_PORT_SOURCE_MASK == YM_PORT_M68K_ADDR_TAG]
    direct_ym = []
    for bus_event in events:
        if (bus_event.device != M68K or bus_event.operation != WRITE or
                bus_event.width not in (1, 2) or
                not 0xA04000 <= bus_event.address < 0xA06000):
            continue
        accepted_value = bus_event.value & 0xFF
        if bus_event.width == 2:
            accepted_value = bus_event.value >> 8 & 0xFF
        port_write = next((event for event in ym_port_writes
                           if event.clock == bus_event.clock and
                           event.address & 3 == (bus_event.address & 3) and
                           event.value == accepted_value), None)
        if port_write is not None:
            direct_ym.append({"cpu_bus": _event_record(bus_event, include_value=False),
                              "ym_port": _event_record(port_write, include_value=False)})
    features["direct_68k_ym_writes"] = _observation(
        capabilities, ["m68k_cpu_access", "ym2612_port_write"],
        direct_ym,
        scope_reason=_scope_reason(manifest, "m68k_cpu_access", "ym_ports"))

    features["six_button_probe"] = _six_button_probe(events, capabilities, manifest)

    mapper_writes = [event for event in events if event.device == M68K and event.operation == WRITE and
                     0xA13000 <= event.address < 0xA13100]
    features["mapper_register_access"] = _observation(
        capabilities, ["m68k_cpu_access"],
        [_event_record(event, include_value=False) for event in mapper_writes],
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
            capabilities, ["m68k_cpu_access"],
            [_event_record(event, include_value=False) for event in sram_events],
            {"start": start, "end": end},
            _scope_reason(manifest, "m68k_cpu_access", "sram_window"))

    report_scope = manifest.get("trace_scope")
    if report_scope is None:
        report_scope = "unknown" if manifest.get("capture_mode") == "feature-only" else "full-candidate-trace"
    elif (manifest.get("capture_mode") == "feature-only" and
          report_scope == "full-candidate-trace"):
        report_scope = "unknown"

    return {
        "core_sha256": manifest.get("core_sha256"),
        "frames": frames,
        "input": manifest.get("input", {}),
        "trace_capabilities": sorted(capabilities),
        "trace_scope": report_scope,
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
