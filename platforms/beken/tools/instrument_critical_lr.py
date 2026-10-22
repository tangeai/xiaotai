#!/usr/bin/env python3
"""Add a build-only CP snapshot of the paired AP critical-section records.

The AP build must enable CONFIG_CRITICAL_LR_RECORD. Apply this after building
that AP ELF, rebuild the CP/package, then restore the SDK source. The addresses
are valid only for the exact AP ELF used to build the package.
"""

import argparse
import hashlib
import re
import subprocess
from pathlib import Path


SYMBOLS = (
    "debug_port_wait",
    "debug_event_fault",
    "debug_queue_wait",
    "debug_event_wait",
    "port_uxCriticalNestingCheckBackup",
    "xKernelLock",
    "xTickCount",
    "pxCurrentTCBs",
    "port_uxCriticalNesting",
    "port_uxEnterCriticalLrStackCount",
    "port_uxEnterCriticalLrStack",
    "port_uxEnterCriticalLrRecordCount",
    "port_uxEnterCriticalLrRecord",
)
MARKER = b"/* DEBUG-bk-hb-critical-lr-snapshot */"
INSERT_BEFORE = b"void mb_ipc_heartbeat_notify(u32 cpu_id);"
TIMEOUT_ANCHOR = b'\t\t\t\tBK_DUMP_OUT("[DEBUG-bk-hb] ap-hw '
MAX_DEPTH = 20


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def addresses(nm_text):
    found = {}
    for line in nm_text.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+[A-Za-z]\s+(\S+)", line)
        if match and match.group(2) in SYMBOLS:
            found[match.group(2)] = int(match.group(1), 16)
    missing = set(SYMBOLS) - found.keys()
    if missing:
        raise ValueError(f"AP ELF missing critical LR symbols: {sorted(missing)}")
    if not all(0x28000000 <= value < 0x28080000 for value in found.values()):
        raise ValueError(f"AP symbol outside CP-readable shared SRAM: {found}")
    return found


def helper(symbols):
    definitions = "\n".join(
        f"#define AP_{name.upper()}_ADDR 0x{symbols[name]:08x}UL"
        for name in SYMBOLS
    )
    source = f"""{MARKER.decode()}
/* AP ELF addresses are paired with this CP image. The LR arrays contain two
 * cores with {MAX_DEPTH} 32-bit entries per core; read only after the timeout. */
{definitions}
#define AP_LR_MAX_DEPTH {MAX_DEPTH}U

static void debug_ap_critical_lr_snapshot(u32 sample)
{{
    u32 run = REG_READ(SOC_SYS_REG_BASE + (0x3 << 2));
    u32 core;
    if (run & ((1U << 9) | (1U << 10))) {{
        BK_DUMP_OUT("[DEBUG-bk-hb] ap-lr sample=%u skipped=powerdown\\r\\n", sample);
        return;
    }}
    BK_DUMP_OUT("[DEBUG-bk-hb] ap-lr sample=%u tick=%u kernel=0x%08x/%u "
                "tcb0=0x%08x tcb1=0x%08x pm-lv=%u pm-mb=%u\\r\\n",
                sample, REG_READ(AP_XTICKCOUNT_ADDR),
                REG_READ(AP_XKERNELLOCK_ADDR), REG_READ(AP_XKERNELLOCK_ADDR + 4U),
                REG_READ(AP_PXCURRENTTCBS_ADDR), REG_READ(AP_PXCURRENTTCBS_ADDR + 4U),
                (u32)bk_pm_module_lv_sleep_state_get(PM_DEV_ID_DEFAULT),
                (u32)s_mb_ipc_work_state);
    for (core = 0; core < 2U; core++) {{
        u32 depth = REG_READ(AP_PORT_UXENTERCRITICALLRSTACKCOUNT_ADDR + core * 4U);
        u32 nesting = REG_READ(AP_PORT_UXCRITICALNESTING_ADDR + core * 4U);
        u32 cursor = REG_READ(AP_PORT_UXENTERCRITICALLRRECORDCOUNT_ADDR + core * 4U);
        u32 i;
        u32 p = AP_DEBUG_PORT_WAIT_ADDR + core * 32U;
        u32 e = AP_DEBUG_EVENT_WAIT_ADDR + core * 32U;
        BK_DUMP_OUT("[DEBUG-bk-hb] ap-port core=%u phase=%u mux=0x%08x owner=%u count=%u irq=%u before=%u lr=0x%08x after=%u backup=%u\\r\\n",
                    core, REG_READ(p), REG_READ(p+4U), REG_READ(p+8U),
                    REG_READ(p+12U), REG_READ(p+16U), REG_READ(p+20U),
                    REG_READ(p+24U), REG_READ(p+28U),
                    REG_READ(AP_PORT_UXCRITICALNESTINGCHECKBACKUP_ADDR + core*4U));
        BK_DUMP_OUT("[DEBUG-bk-hb] ap-event core=%u phase=%u list=0x%08x event=0x%08x state=0x%08x\\r\\n",
                    core, REG_READ(e), REG_READ(e+4U), REG_READ(e+8U),
                    REG_READ(e+12U));
        BK_DUMP_OUT("[DEBUG-bk-hb] ap-event-tail core=%u delayed=0x%08x wait=%u tcb=0x%08x items=%u\\r\\n",
                    core, REG_READ(e+16U), REG_READ(e+20U),
                    REG_READ(e+24U), REG_READ(e+28U));
        if (REG_READ(AP_DEBUG_EVENT_FAULT_ADDR + core*64U) != 0U) {{
            /* Four words per line stay below the shell formatter limit. */
            u32 f = AP_DEBUG_EVENT_FAULT_ADDR + core*64U;
            u32 q = AP_DEBUG_QUEUE_WAIT_ADDR + core*96U;
            for (i = 0U; i < 16U; i += 4U) {{
                BK_DUMP_OUT("[DEBUG-bk-hb] ap-first-reentry core=%u offset=%u words=%08x,%08x,%08x,%08x\\r\\n",
                            core, i, REG_READ(f+i*4U), REG_READ(f+i*4U+4U),
                            REG_READ(f+i*4U+8U), REG_READ(f+i*4U+12U));
            }}
            for (i = 0U; i < 24U; i += 4U) {{
                BK_DUMP_OUT("[DEBUG-bk-hb] ap-queue-boundary core=%u offset=%u words=%08x,%08x,%08x,%08x\\r\\n",
                            core, i, REG_READ(q+i*4U), REG_READ(q+i*4U+4U),
                            REG_READ(q+i*4U+8U), REG_READ(q+i*4U+12U));
            }}
        }}
        if (REG_READ(e) != 0U) {{
            /* Fixed six-word windows, no pointer chasing on a damaged list. */
            for (i = 1U; i <= 4U; i++) {{
                u32 address = REG_READ(e + i*4U);
                if ((address & 3U) == 0U &&
                    ((address >= 0x28000000U && address <= 0x2809ffe8U) ||
                     (address >= 0x60550000U && address <= 0x60fdffe8U))) {{
                    BK_DUMP_OUT("[DEBUG-bk-hb] ap-event-memory core=%u slot=%u addr=0x%08x words=%08x,%08x,%08x,%08x,%08x,%08x\\r\\n",
                                core, i, address, REG_READ(address),
                                REG_READ(address+4U), REG_READ(address+8U),
                                REG_READ(address+12U), REG_READ(address+16U),
                                REG_READ(address+20U));
                }}
            }}
        }}
        BK_DUMP_OUT("[DEBUG-bk-hb] ap-lr core=%u depth=%u nesting=%u cursor=%u\\r\\n",
                    core, depth, nesting, cursor);
        if (depth > AP_LR_MAX_DEPTH) {{
            depth = AP_LR_MAX_DEPTH;
        }}
        for (i = 0; i < depth; i++) {{
            BK_DUMP_OUT("[DEBUG-bk-hb] ap-lr core=%u stack=%u pc=0x%08x\\r\\n",
                        core, i,
                        REG_READ(AP_PORT_UXENTERCRITICALLRSTACK_ADDR +
                                 (core * AP_LR_MAX_DEPTH + i) * 4U));
        }}
        if (cursor < AP_LR_MAX_DEPTH) {{
            for (i = 0; i < 4U; i++) {{
                u32 slot = (cursor + AP_LR_MAX_DEPTH - 1U - i) % AP_LR_MAX_DEPTH;
                BK_DUMP_OUT("[DEBUG-bk-hb] ap-lr core=%u recent=%u pc=0x%08x\\r\\n",
                            core, i,
                            REG_READ(AP_PORT_UXENTERCRITICALLRRECORD_ADDR +
                                     (core * AP_LR_MAX_DEPTH + slot) * 4U));
            }}
        }}
    }}
}}

"""
    return source.replace("\n", "\r\n").encode()


def instrument(source, symbols):
    if source.count(INSERT_BEFORE) != 1 or source.count(TIMEOUT_ANCHOR) != 1:
        raise ValueError("CP heartbeat insertion point is not unique")
    updated = source.replace(INSERT_BEFORE, helper(symbols) + INSERT_BEFORE)
    return updated.replace(
        TIMEOUT_ANCHOR,
        b"\t\t\t\tdebug_ap_critical_lr_snapshot(0);\r\n"
        b"\t\t\t\trtos_delay_milliseconds(20);\r\n"
        b"\t\t\t\tdebug_ap_critical_lr_snapshot(1);\r\n"
        + TIMEOUT_ANCHOR,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("apply", "verify", "restore"))
    parser.add_argument("--sdk-file", type=Path, required=True)
    parser.add_argument("--ap-elf", type=Path)
    parser.add_argument("--nm-file", type=Path)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--expected-source-sha256")
    args = parser.parse_args()
    if args.action == "restore":
        current = args.sdk_file.read_bytes()
        original = args.backup.read_bytes()
        if MARKER not in current or args.expected_source_sha256 != sha256(original):
            raise ValueError("refusing to restore unexpected SDK source")
        args.sdk_file.write_bytes(original)
        return
    if (args.ap_elf is None) == (args.nm_file is None):
        parser.error("supply exactly one of --ap-elf and --nm-file")
    nm_text = (subprocess.check_output(["nm", "-an", str(args.ap_elf)], text=True)
               if args.ap_elf else args.nm_file.read_text())
    symbols = addresses(nm_text)
    if args.action == "verify":
        expected = dict(line.split("=", 1) for line in args.contract.read_text().splitlines())
        if any(symbols[name] != int(expected[name], 16) for name in SYMBOLS):
            raise ValueError("AP ELF symbol address changed after CP instrumentation")
        return
    source = args.sdk_file.read_bytes()
    if args.expected_source_sha256 != sha256(source):
        raise ValueError("refusing to instrument unexpected SDK source")
    updated = instrument(source, symbols)
    args.backup.write_bytes(source)
    args.contract.write_text("".join(f"{name}=0x{symbols[name]:08x}\n" for name in SYMBOLS))
    args.sdk_file.write_bytes(updated)


if __name__ == "__main__":
    main()
