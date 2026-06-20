#!/usr/bin/env python3
"""Static USB descriptor gate for RTT ArduPilot firmware images.

This gate parses the linked ``rtthread.bin`` and verifies the USB descriptor
bytes that Windows uses for driver binding.  It is intentionally independent of
hardware so a build can be rejected before flashing when the RTT CherryUSB
identity drifts away from ArduPilot/ChibiOS.
"""

from __future__ import annotations

import argparse
import json
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


DEFAULT_BIN = "build/rtt_cuav_v5/rtthread.bin"

USB_DESC_DEVICE = 0x01
USB_DESC_CONFIGURATION = 0x02
USB_DESC_STRING = 0x03
USB_DESC_INTERFACE = 0x04
USB_DESC_ENDPOINT = 0x05
USB_DESC_INTERFACE_ASSOCIATION = 0x0B
USB_DESC_CS_INTERFACE = 0x24


@dataclass
class DescriptorView:
    offset: int
    length: int
    dtype: int
    data: bytes

    def json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "offset": hex(self.offset),
            "length": self.length,
            "type": self.dtype,
            "hex": self.data.hex(),
        }
        if self.dtype == USB_DESC_CONFIGURATION:
            payload.update({
                "kind": "configuration",
                "totalLength": self.data[2] | (self.data[3] << 8),
                "bNumInterfaces": self.data[4],
                "bConfigurationValue": self.data[5],
                "iConfiguration": self.data[6],
                "bmAttributes": f"0x{self.data[7]:02x}",
                "bMaxPower": self.data[8],
            })
        elif self.dtype == USB_DESC_INTERFACE_ASSOCIATION:
            payload.update({
                "kind": "iad",
                "bFirstInterface": self.data[2],
                "bInterfaceCount": self.data[3],
                "bFunctionClass": self.data[4],
                "bFunctionSubClass": self.data[5],
                "bFunctionProtocol": self.data[6],
                "iFunction": self.data[7],
            })
        elif self.dtype == USB_DESC_INTERFACE:
            payload.update({
                "kind": "interface",
                "bInterfaceNumber": self.data[2],
                "bAlternateSetting": self.data[3],
                "bNumEndpoints": self.data[4],
                "bInterfaceClass": self.data[5],
                "bInterfaceSubClass": self.data[6],
                "bInterfaceProtocol": self.data[7],
                "iInterface": self.data[8],
            })
        elif self.dtype == USB_DESC_ENDPOINT:
            payload.update({
                "kind": "endpoint",
                "bEndpointAddress": f"0x{self.data[2]:02x}",
                "bmAttributes": f"0x{self.data[3]:02x}",
                "wMaxPacketSize": self.data[4] | (self.data[5] << 8),
                "bInterval": self.data[6],
            })
        elif self.dtype == USB_DESC_CS_INTERFACE:
            payload.update({
                "kind": "cdc_functional",
                "bDescriptorSubtype": f"0x{self.data[2]:02x}",
            })
        else:
            payload["kind"] = "other"
        return payload


def u16le(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def find_device_descriptors(image: bytes, vid: int, pid: int) -> list[int]:
    offsets: list[int] = []
    for index in range(0, len(image) - 18):
        if image[index] != 18 or image[index + 1] != USB_DESC_DEVICE:
            continue
        if u16le(image, index + 8) == vid and u16le(image, index + 10) == pid:
            offsets.append(index)
    return offsets


def find_config_after_device(image: bytes, dev_offset: int, *, max_scan: int = 512) -> int | None:
    end = min(len(image) - 9, dev_offset + max_scan)
    for index in range(dev_offset + 18, end):
        if image[index] != 9 or image[index + 1] != USB_DESC_CONFIGURATION:
            continue
        total = u16le(image, index + 2)
        if 9 <= total <= 512 and index + total <= len(image):
            return index
    return None


def parse_config(image: bytes, offset: int) -> list[DescriptorView]:
    total = u16le(image, offset + 2)
    cfg = image[offset:offset + total]
    descriptors: list[DescriptorView] = []
    cursor = 0
    while cursor + 2 <= len(cfg):
        length = cfg[cursor]
        if length == 0:
            break
        if cursor + length > len(cfg):
            descriptors.append(DescriptorView(offset + cursor, len(cfg) - cursor, -1, cfg[cursor:]))
            break
        descriptors.append(DescriptorView(offset + cursor, length, cfg[cursor + 1], cfg[cursor:cursor + length]))
        cursor += length
    return descriptors


def decode_usb_string(data: bytes) -> str | None:
    if len(data) < 2 or data[1] != USB_DESC_STRING:
        return None
    try:
        return data[2:].decode("utf-16-le")
    except UnicodeDecodeError:
        return None


def parse_strings_after_config(image: bytes, cfg_offset: int | None) -> list[dict[str, Any]]:
    if cfg_offset is None:
        return []
    total = u16le(image, cfg_offset + 2)
    cursor = cfg_offset + total
    strings: list[dict[str, Any]] = []
    index = 0
    while cursor + 2 <= len(image):
        length = image[cursor]
        if length == 0:
            break
        if length < 2 or cursor + length > len(image):
            break
        dtype = image[cursor + 1]
        if dtype != USB_DESC_STRING:
            break
        data = image[cursor:cursor + length]
        item: dict[str, Any] = {
            "index": index,
            "offset": hex(cursor),
            "length": length,
            "type": dtype,
            "hex": data.hex(),
        }
        if index == 0:
            item["langids"] = [
                f"0x{u16le(data, pos):04x}"
                for pos in range(2, len(data) - 1, 2)
            ]
        else:
            item["text"] = decode_usb_string(data)
        strings.append(item)
        cursor += length
        index += 1
    return strings


def check(name: str, passed: bool, evidence: Any = None, reason: str | None = None) -> dict[str, Any]:
    item: dict[str, Any] = {
        "name": name,
        "status": "PASS" if passed else "FAIL",
    }
    if evidence is not None:
        item["evidence"] = evidence
    if reason:
        item["reason"] = reason
    return item


def descriptor_summary(image: bytes, *, vid: int, pid: int) -> dict[str, Any]:
    offsets = find_device_descriptors(image, vid, pid)
    if not offsets:
        return {
            "device_descriptor_found": False,
            "device_offsets": [],
            "checks": [check("device_descriptor_present", False, reason="vid_pid_not_found")],
        }

    dev_offset = offsets[0]
    dev = image[dev_offset:dev_offset + 18]
    cfg_offset = find_config_after_device(image, dev_offset)
    descriptors = parse_config(image, cfg_offset) if cfg_offset is not None else []
    strings = parse_strings_after_config(image, cfg_offset)
    return {
        "device_descriptor_found": True,
        "device_offsets": [hex(offset) for offset in offsets],
        "selected_device_offset": hex(dev_offset),
        "device": {
            "bcdUSB": f"0x{u16le(dev, 2):04x}",
            "bDeviceClass": dev[4],
            "bDeviceSubClass": dev[5],
            "bDeviceProtocol": dev[6],
            "bMaxPacketSize0": dev[7],
            "idVendor": f"0x{u16le(dev, 8):04x}",
            "idProduct": f"0x{u16le(dev, 10):04x}",
            "bcdDevice": f"0x{u16le(dev, 12):04x}",
            "iManufacturer": dev[14],
            "iProduct": dev[15],
            "iSerialNumber": dev[16],
            "bNumConfigurations": dev[17],
        },
        "config_offset": hex(cfg_offset) if cfg_offset is not None else None,
        "descriptors": [descriptor.json() for descriptor in descriptors],
        "strings": strings,
    }


def audit_dual_chibios(summary: dict[str, Any]) -> dict[str, Any]:
    dev = summary.get("device") or {}
    descs = summary.get("descriptors") or []
    strings = summary.get("strings") or []
    strings_by_index = {item.get("index"): item for item in strings}
    serial = (strings_by_index.get(3) or {}).get("text") or ""
    configs = [d for d in descs if d.get("kind") == "configuration"]
    iads = [d for d in descs if d.get("kind") == "iad"]
    interfaces = [d for d in descs if d.get("kind") == "interface"]
    endpoints = [d for d in descs if d.get("kind") == "endpoint"]
    cdc = [d for d in descs if d.get("kind") == "cdc_functional"]

    expected_iads = [
        {"bFirstInterface": 0, "bInterfaceCount": 2, "bFunctionClass": 2, "bFunctionSubClass": 2, "bFunctionProtocol": 1, "iFunction": 0},
        {"bFirstInterface": 2, "bInterfaceCount": 2, "bFunctionClass": 2, "bFunctionSubClass": 2, "bFunctionProtocol": 1, "iFunction": 0},
    ]
    expected_interfaces = [
        {"bInterfaceNumber": 0, "bNumEndpoints": 1, "bInterfaceClass": 2, "bInterfaceSubClass": 2, "bInterfaceProtocol": 1, "iInterface": 0},
        {"bInterfaceNumber": 1, "bNumEndpoints": 2, "bInterfaceClass": 10, "bInterfaceSubClass": 0, "bInterfaceProtocol": 0, "iInterface": 0},
        {"bInterfaceNumber": 2, "bNumEndpoints": 1, "bInterfaceClass": 2, "bInterfaceSubClass": 2, "bInterfaceProtocol": 1, "iInterface": 0},
        {"bInterfaceNumber": 3, "bNumEndpoints": 2, "bInterfaceClass": 10, "bInterfaceSubClass": 0, "bInterfaceProtocol": 0, "iInterface": 0},
    ]
    expected_endpoints = [
        ("0x81", "0x03", 16, 1),
        ("0x02", "0x02", 64, 0),
        ("0x82", "0x02", 64, 0),
        ("0x83", "0x03", 16, 1),
        ("0x04", "0x02", 64, 0),
        ("0x84", "0x02", 64, 0),
    ]
    actual_iads = [
        {k: iad.get(k) for k in expected_iads[0]}
        for iad in iads
    ]
    actual_interfaces = [
        {k: intf.get(k) for k in expected_interfaces[0]}
        for intf in interfaces
    ]
    actual_endpoints = [
        (ep.get("bEndpointAddress"), ep.get("bmAttributes"), ep.get("wMaxPacketSize"), ep.get("bInterval"))
        for ep in endpoints
    ]
    cdc_hex = [d.get("hex") for d in cdc]

    checks = [
        check("device_descriptor_present", bool(summary.get("device_descriptor_found")), summary.get("device_offsets")),
        check("vid_pid_1209_5740", dev.get("idVendor") == "0x1209" and dev.get("idProduct") == "0x5740", dev),
        check("device_misc_iad_class", (dev.get("bDeviceClass"), dev.get("bDeviceSubClass"), dev.get("bDeviceProtocol")) == (0xEF, 0x02, 0x01),
              {k: dev.get(k) for k in ("bDeviceClass", "bDeviceSubClass", "bDeviceProtocol")}),
        check("bcd_usb_and_device_match_chibios", dev.get("bcdUSB") == "0x0200" and dev.get("bcdDevice") == "0x0200",
              {"bcdUSB": dev.get("bcdUSB"), "bcdDevice": dev.get("bcdDevice")}),
        check("device_string_indices_chibios", (dev.get("iManufacturer"), dev.get("iProduct"), dev.get("iSerialNumber")) == (1, 2, 3),
              {k: dev.get(k) for k in ("iManufacturer", "iProduct", "iSerialNumber")}),
        check("configuration_present", len(configs) == 1, configs),
        check("configuration_shape", bool(configs) and configs[0].get("totalLength") == 141 and configs[0].get("bNumInterfaces") == 4,
              configs[0] if configs else None),
        check("configuration_power_chibios_100ma", bool(configs) and configs[0].get("bmAttributes") == "0xc0" and configs[0].get("bMaxPower") == 50,
              configs[0] if configs else None),
        check("iad_mi00_mi02", actual_iads == expected_iads, {"actual": actual_iads, "expected": expected_iads}),
        check("interfaces_chibios_no_strings", actual_interfaces == expected_interfaces,
              {"actual": actual_interfaces, "expected": expected_interfaces}),
        check("endpoint_order_and_sizes", actual_endpoints == expected_endpoints,
              {"actual": actual_endpoints, "expected": expected_endpoints}),
        check("cdc_functional_descriptors", cdc_hex == [
            "0524001001", "0524010301", "04240202", "0524060001",
            "0524001001", "0524010303", "04240202", "0524060203",
        ], cdc_hex),
        check("string0_english_us", (strings_by_index.get(0) or {}).get("langids") == ["0x0409"],
              strings_by_index.get(0)),
        check("string1_manufacturer_ardupilot", (strings_by_index.get(1) or {}).get("text") == "ArduPilot",
              strings_by_index.get(1)),
        check("string2_product_cuavv5", (strings_by_index.get(2) or {}).get("text") == "CUAVv5",
              strings_by_index.get(2)),
        check("string3_serial_hex24_or_static_placeholder",
              len(serial) == 24 and all(ch in "0123456789ABCDEF" for ch in serial),
              strings_by_index.get(3),
              reason="static bin may contain 24 zero placeholder; runtime must patch STM32 UID before descriptor registration"),
        check("string_table_chibios_only_0_to_3",
              len(strings) == 4 and {item.get("index") for item in strings} == {0, 1, 2, 3},
              strings,
              reason="ChibiOS dual CDC exposes only language/manufacturer/product/serial strings; CDC/IAD/interface string indices stay 0"),
    ]
    return {
        "profile": "chibios_dualcdc",
        "checks": checks,
        "verdict": "GREEN" if all(item["status"] == "PASS" for item in checks) else "RED",
    }


def audit_mavlink_only(summary: dict[str, Any]) -> dict[str, Any]:
    dev = summary.get("device") or {}
    descs = summary.get("descriptors") or []
    configs = [d for d in descs if d.get("kind") == "configuration"]
    interfaces = [d for d in descs if d.get("kind") == "interface"]
    endpoints = [d for d in descs if d.get("kind") == "endpoint"]
    actual_endpoints = [
        (ep.get("bEndpointAddress"), ep.get("bmAttributes"), ep.get("wMaxPacketSize"), ep.get("bInterval"))
        for ep in endpoints
    ]
    checks = [
        check("device_descriptor_present", bool(summary.get("device_descriptor_found")), summary.get("device_offsets")),
        check("vid_pid_1209_5741", dev.get("idVendor") == "0x1209" and dev.get("idProduct") == "0x5741", dev),
        check("single_cdc_device_class", (dev.get("bDeviceClass"), dev.get("bDeviceSubClass"), dev.get("bDeviceProtocol")) == (0x02, 0x00, 0x00),
              {k: dev.get(k) for k in ("bDeviceClass", "bDeviceSubClass", "bDeviceProtocol")}),
        check("two_interfaces", bool(configs) and configs[0].get("bNumInterfaces") == 2, configs[0] if configs else None),
        check("endpoint_order_single_cdc", actual_endpoints == [("0x82", "0x03", 8, 255), ("0x01", "0x02", 64, 0), ("0x81", "0x02", 64, 0)],
              actual_endpoints),
        check("interface_string_present_for_diagnostic", bool(interfaces) and interfaces[0].get("iInterface") != 0,
              interfaces[0] if interfaces else None),
    ]
    return {
        "profile": "mavlink_only",
        "checks": checks,
        "verdict": "GREEN" if all(item["status"] == "PASS" for item in checks) else "RED",
    }


def run(bin_path: Path, profile: str) -> dict[str, Any]:
    image = bin_path.read_bytes()
    vid = 0x1209
    pid = 0x5741 if profile == "mavlink_only" else 0x5740
    summary = descriptor_summary(image, vid=vid, pid=pid)
    audit = audit_mavlink_only(summary) if profile == "mavlink_only" else audit_dual_chibios(summary)
    return {
        "bin": str(bin_path),
        "size": len(image),
        "descriptor": summary,
        "audit": audit,
        "verdict": audit["verdict"],
    }


def build_dual_descriptor_image(*, pid: int = 0x5740, bcd_device: int = 0x0200,
                                max_power: int = 50, interface_string: int = 0,
                                product: str = "CUAVv5",
                                extra_interface_strings: bool = False) -> bytes:
    device = bytes([
        18, USB_DESC_DEVICE, 0x00, 0x02, 0xEF, 0x02, 0x01, 64,
        0x09, 0x12, pid & 0xFF, (pid >> 8) & 0xFF,
        bcd_device & 0xFF, (bcd_device >> 8) & 0xFF,
        1, 2, 3, 1,
    ])
    cfg = bytes([
        9, USB_DESC_CONFIGURATION, 0x8D, 0x00, 4, 1, 0, 0xC0, max_power,
        8, USB_DESC_INTERFACE_ASSOCIATION, 0, 2, 2, 2, 1, 0,
        9, USB_DESC_INTERFACE, 0, 0, 1, 2, 2, 1, interface_string,
        5, USB_DESC_CS_INTERFACE, 0x00, 0x10, 0x01,
        5, USB_DESC_CS_INTERFACE, 0x01, 0x03, 0x01,
        4, USB_DESC_CS_INTERFACE, 0x02, 0x02,
        5, USB_DESC_CS_INTERFACE, 0x06, 0x00, 0x01,
        7, USB_DESC_ENDPOINT, 0x81, 0x03, 0x10, 0x00, 1,
        9, USB_DESC_INTERFACE, 1, 0, 2, 10, 0, 0, 0,
        7, USB_DESC_ENDPOINT, 0x02, 0x02, 0x40, 0x00, 0,
        7, USB_DESC_ENDPOINT, 0x82, 0x02, 0x40, 0x00, 0,
        8, USB_DESC_INTERFACE_ASSOCIATION, 2, 2, 2, 2, 1, 0,
        9, USB_DESC_INTERFACE, 2, 0, 1, 2, 2, 1, interface_string,
        5, USB_DESC_CS_INTERFACE, 0x00, 0x10, 0x01,
        5, USB_DESC_CS_INTERFACE, 0x01, 0x03, 0x03,
        4, USB_DESC_CS_INTERFACE, 0x02, 0x02,
        5, USB_DESC_CS_INTERFACE, 0x06, 0x02, 0x03,
        7, USB_DESC_ENDPOINT, 0x83, 0x03, 0x10, 0x00, 1,
        9, USB_DESC_INTERFACE, 3, 0, 2, 10, 0, 0, 0,
        7, USB_DESC_ENDPOINT, 0x04, 0x02, 0x40, 0x00, 0,
        7, USB_DESC_ENDPOINT, 0x84, 0x02, 0x40, 0x00, 0,
    ])
    return (
        b"\xAA" * 32 +
        device +
        cfg +
        build_string_descriptors(product=product, extra_interface_strings=extra_interface_strings) +
        b"\x00"
    )


def usb_string(text: str) -> bytes:
    raw = text.encode("utf-16-le")
    return bytes([len(raw) + 2, USB_DESC_STRING]) + raw


def build_string_descriptors(*, product: str, extra_interface_strings: bool = False) -> bytes:
    strings = [
        bytes([4, USB_DESC_STRING, 0x09, 0x04]),
        usb_string("ArduPilot"),
        usb_string(product),
        usb_string("000000000000000000000000"),
    ]
    if extra_interface_strings:
        strings.extend([
            usb_string("MAVLink CDC"),
            usb_string("SLCAN CDC"),
        ])
    return b"".join(strings)


def self_test() -> dict[str, Any]:
    cases = [
        ("valid_chibios_dualcdc", build_dual_descriptor_image(), "GREEN"),
        ("wrong_bcd_device", build_dual_descriptor_image(bcd_device=0x0207), "RED"),
        ("wrong_power", build_dual_descriptor_image(max_power=25), "RED"),
        ("interface_strings_not_chibios", build_dual_descriptor_image(interface_string=4), "RED"),
        ("extra_unreferenced_strings_not_chibios", build_dual_descriptor_image(extra_interface_strings=True), "RED"),
        ("wrong_product_string", build_dual_descriptor_image(product="CUAV V5 RTT"), "RED"),
        ("wrong_pid", build_dual_descriptor_image(pid=0x5741), "RED"),
    ]
    results: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="rtt_usb_descriptor_gate_") as tmp:
        root = Path(tmp)
        for name, image, want in cases:
            path = root / f"{name}.bin"
            path.write_bytes(image)
            got = run(path, "chibios_dualcdc")["verdict"]
            results.append({
                "name": name,
                "want": want,
                "got": got,
                "passed": got == want,
            })
    return {
        "tests": results,
        "verdict": "GREEN" if all(item["passed"] for item in results) else "RED",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", default=DEFAULT_BIN, help="firmware binary to inspect")
    parser.add_argument("--profile", choices=("chibios_dualcdc", "mavlink_only"), default="chibios_dualcdc")
    parser.add_argument("--json-out", help="optional path for JSON report")
    parser.add_argument("--self-test", action="store_true", help="run built-in descriptor gate tests")
    args = parser.parse_args()

    report = self_test() if args.self_test else run(Path(args.bin), args.profile)
    text = json.dumps(report, indent=2, ensure_ascii=False)
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report["verdict"] == "GREEN" else 2


if __name__ == "__main__":
    raise SystemExit(main())
