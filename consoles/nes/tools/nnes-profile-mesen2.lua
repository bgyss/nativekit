-- Optional raw trace capture for tools/nnes-profile.
-- Requires the optype patch in nnes-profile-mesen2-optype.patch.

local function integer_setting(name, default_value, minimum, maximum)
    local raw = os.getenv(name)
    if raw == nil or raw == "" then return default_value end
    local value = tonumber(raw)
    if not value or value < minimum or value > maximum or
       value ~= math.floor(value) then
        error(name .. " must be an integer from " .. minimum .. " through " .. maximum)
    end
    return value
end

local WARMUP_FRAMES = integer_setting("NNES_PROFILE_WARMUP_FRAMES", 120, 0, 100000)
local CAPTURE_FRAMES = integer_setting("NNES_PROFILE_CAPTURE_FRAMES", 600, 1, 100000)
local warmup_remaining = WARMUP_FRAMES
local STOP_ON_FINISH = os.getenv("NNES_PROFILE_STOP_ON_FINISH") == "1"
-- Add the Mesen2 release or commit built with the patch to this reference.
local REFERENCE = os.getenv("NNES_PROFILE_REFERENCE") or
                  "Mesen2 + nnes operation-type callback patch"
if string.find(REFERENCE, "[\r\n]") then
    error("NNES_PROFILE_REFERENCE must be a single line")
end

-- Add mapper-specific RAM or I/O address ranges here. Ranges are applied only
-- to cartridge space ($4020-$FFFF), matching nnes_cartridge.access_kind.
local MEMORY_RANGES = {
    -- { first = 0x6000, last = 0x7FFF, kind = "cart_ram" },
}
local configured_ranges = os.getenv("NNES_PROFILE_MEMORY_RANGES")
if configured_ranges and configured_ranges ~= "" then
    MEMORY_RANGES = {}
    for item in string.gmatch(configured_ranges, "[^,]+") do
        local first, last, kind = string.match(item,
            "^([%x]+)%-([%x]+):([%a_]+)$")
        if not first then
            error("NNES_PROFILE_MEMORY_RANGES entries must look like 6000-7FFF:cart_ram")
        end
        MEMORY_RANGES[#MEMORY_RANGES + 1] = {
            first = tonumber(first, 16), last = tonumber(last, 16), kind = kind
        }
    end
end

local KIND_NAMES = { wram = true, rom = true, cart_ram = true, io = true }
for _, range in ipairs(MEMORY_RANGES) do
    if type(range.first) ~= "number" or type(range.last) ~= "number" or
       range.first < 0x4020 or range.last > 0xFFFF or
       range.first > range.last or not KIND_NAMES[range.kind] then
        error("MEMORY_RANGES entries must be valid cartridge address ranges and access kinds")
    end
end
local kind_map = "nnes-profile-kinds-v1"
local operations = emu.memoryOperationType
if not operations then
    error("Mesen2 operation-type patch is required; emu.memoryOperationType is missing")
end

local operation_names = {
    { key = "read", name = "read" },
    { key = "write", name = "write" },
    { key = "execOpCode", name = "opcode" },
    { key = "execOperand", name = "operand" },
    { key = "dummyRead", name = "dummy_read" },
    { key = "dummyWrite", name = "dummy_write" },
    { key = "dmaRead", name = "dma_read" },
    { key = "dmaWrite", name = "dma_write" },
    { key = "ppuRenderingRead", name = "ppu_rendering_read" },
    { key = "idle", name = "idle" },
}

local operation_by_value = {}
for _, item in ipairs(operation_names) do
    local value = operations[item.key]
    if type(value) ~= "number" then
        error("Mesen2 operation-type patch is missing emu.memoryOperationType." .. item.key)
    end
    operation_by_value[value] = item.name
end

local cpu_type = emu.cpuType.nes
local memory_type = emu.memType.nesMemory
local read_callback_type = emu.callbackType.read
local write_callback_type = emu.callbackType.write
local exec_callback_type = emu.callbackType.exec
local end_frame_event = emu.eventType.endFrame

local data_folder = emu.getScriptDataFolder()
if data_folder == "" or not io or not io.open or not os or not os.date then
    error("Enable Mesen's 'Allow access to I/O and OS functions' script setting")
end
local OUTPUT_NAME = os.getenv("NNES_PROFILE_OUTPUT_NAME") or
                    "nnes-profile-raw-" .. os.date("%Y%m%d-%H%M%S") .. ".jsonl"
if not string.match(OUTPUT_NAME, "^[%w._-]+$") then
    error("NNES_PROFILE_OUTPUT_NAME must be a plain filename")
end

local rom_info = emu.getRomInfo()
local rom_sha1 = string.lower(rom_info.fileSha1Hash or "")
if not string.match(rom_sha1, "^%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x$") then
    error("Mesen did not provide a 40-character ROM SHA-1")
end

local output = nil
local capture_started = false
local capture_finished = false
local capture_frames_done = 0
local cycle_offset = 0
local previous_raw_cycle = nil
local previous_cycle = nil

local function finish(message, exit_code)
    if capture_finished then return end
    capture_finished = true
    if output then
        output:flush()
        output:close()
        output = nil
    end
    emu.log(message)
    if STOP_ON_FINISH then emu.stop(exit_code or 0) end
end

local function fail_capture(message)
    finish("nnes-profile capture stopped: " .. message, 2)
end

local function classify(address)
    if address < 0x2000 then return "wram" end
    if address < 0x4020 then return "io" end
    for _, range in ipairs(MEMORY_RANGES) do
        if address >= range.first and address <= range.last then
            if not KIND_NAMES[range.kind] then
                error("MEMORY_RANGES has an invalid kind: " .. tostring(range.kind))
            end
            return range.kind
        end
    end
    return "rom"
end

local function emit_header()
    local header = string.format(
        '{"type":"header","format":"nnes-cpu-op-trace","version":1,' ..
        '"rom_sha1":"%s","reference":"%s",' ..
        '"operation_types_included":true,' ..
        '"cycle_basis":"cpu_cycles_including_dma_stalls",' ..
        '"kind_map":"%s","warmup_frames":%d,"capture_frames":%d}\n',
        rom_sha1, REFERENCE, kind_map, WARMUP_FRAMES, CAPTURE_FRAMES)
    output:write(header)
    output:flush()
end

local function on_memory_access(address, value, operation_type)
    if not capture_started or capture_finished then return end
    local operation = operation_by_value[operation_type]
    if not operation then
        fail_capture("unknown MemoryOperationType value " .. tostring(operation_type))
        return
    end

    local raw_cycle = emu.getCpuCycleCount(cpu_type)
    if type(raw_cycle) ~= "number" then
        fail_capture("Mesen state did not expose NesCpu.CycleCount")
        return
    end
    if not previous_raw_cycle and raw_cycle < 0 then
        cycle_offset = -raw_cycle
    elseif previous_raw_cycle and raw_cycle < previous_raw_cycle then
        cycle_offset = (previous_cycle or 0) + 1 - raw_cycle
    end
    local cycle = cycle_offset + raw_cycle
    if previous_cycle and cycle < previous_cycle then
        fail_capture("CPU cycle timeline moved backwards")
        return
    end
    previous_raw_cycle = raw_cycle
    previous_cycle = cycle

    local address_value = tonumber(address)
    if not address_value or address_value < 0 or address_value > 0xFFFF then
        fail_capture("NES CPU callback address was outside $0000-$FFFF")
        return
    end
    local line = string.format(
        '{"type":"access","frame":%d,"cycle":%d,' ..
        '"operation":"%s","address":%d',
        capture_frames_done, cycle, operation, address_value)
    if operation == "read" or operation == "write" then
        line = line .. string.format(',"kind":"%s"', classify(address_value))
    end
    output:write(line .. "}\n")
end

local function on_end_frame()
    if capture_finished then return end
    if not capture_started then
        if warmup_remaining > 0 then
            warmup_remaining = warmup_remaining - 1
            if warmup_remaining > 0 then return end
        end
        local folder = data_folder
        local separator = string.sub(folder, -1)
        local path = folder .. ((separator == "/" or separator == "\\") and "" or "/") .. OUTPUT_NAME
        output = io.open(path, "w")
        if not output then
            finish("nnes-profile could not open output file: " .. path, 2)
            return
        end
        capture_started = true
        emit_header()
        emu.log("nnes-profile capturing " .. CAPTURE_FRAMES .. " NES frames to " .. path)
        return
    end

    capture_frames_done = capture_frames_done + 1
    output:flush()
    if capture_frames_done >= CAPTURE_FRAMES then
        finish("nnes-profile raw trace complete; run tools/nnes-profile normalize on " .. OUTPUT_NAME)
    end
end

-- Leave no-op callbacks registered after capture. Removing them from inside a
-- callback can mutate Mesen's callback list while it is iterating that list.
for _, callback_type in ipairs({ read_callback_type, write_callback_type, exec_callback_type }) do
    emu.addMemoryCallback(on_memory_access, callback_type, 0, 0xFFFF,
                          cpu_type, memory_type)
end
emu.addEventCallback(on_end_frame, end_frame_event)
emu.log("nnes-profile waiting for " .. WARMUP_FRAMES .. " warmup frames")
