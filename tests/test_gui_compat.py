"""Read-only checks against the user's actual GUI parser without Qt imports.

Usage: python3 tests/test_gui_compat.py /path/to/obc_telemetry_gui_lora_p2p_3tabs.py
"""
import ast
import re
import sys
import time
from pathlib import Path
from typing import Optional

source = ast.parse(Path(sys.argv[1]).read_text())
functions = {"_valid_q", "_record", "parse_telemetry", "parse_reply"}
regexes = {
    "ASCII_RE", "LORA_Q_RE", "PICO_PAYLOAD_RE", "PICO_LENGTH_RE",
    "PICO_RSSI_RE", "PICO_SNR_RE", "PICO_FREQ_RE",
}
nodes = []
for node in source.body:
    if isinstance(node, ast.FunctionDef) and node.name in functions:
        nodes.append(node)
    elif isinstance(node, ast.ClassDef) and node.name == "PicoPacketParser":
        nodes.append(node)
    elif isinstance(node, ast.Assign) and any(
        isinstance(target, ast.Name) and target.id in regexes for target in node.targets
    ):
        nodes.append(node)
namespace = {"re": re, "time": time, "Optional": Optional}
exec(compile(ast.Module(body=nodes, type_ignores=[]), "<GUI parser subset>", "exec"), namespace)
tel = "TEL,14,15984,12,-10,992,0,1,-2,113,-65,80,10000,0,0,0,1"
record = namespace["parse_telemetry"](tel)
assert record["az"] == 992 and record["q0f"] == 1 and record["mag_ok"]
assert namespace["parse_reply"]("PICO_TX,41,SENT")["type"] == "PICO_TX"
assert namespace["parse_reply"]("ACK,41,OK,APPLIED")["type"] == "ACK"
for payload in [tel, "Q,+10000,+00000,+00000,+00000", "TEST_LORA",
                "ACK,41,OK,APPLIED", "EVT,7,CAL_GYRO,DONE,BIAS_APPLIED"]:
    parser = namespace["PicoPacketParser"]()
    packets = []
    for line in [f'[RX] Payload: "{payload}"', f"[RX] Longitud: {len(payload)} bytes",
                 "[RX] RSSI: -70 dBm", "[RX] SNR: 9 dB", "[RX] Freq error: 0 Hz"]:
        packets.extend(parser.feed(line))
    assert len(packets) == 1 and packets[0]["payload_text"] == payload
    assert packets[0]["rssi"] == -70 and packets[0]["snr"] == 9
print("Actual GUI TEL/Q/ACK/EVT/TEST_LORA parser compatibility passed (read-only AST subset)")
