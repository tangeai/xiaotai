# Agent guidance

When changing group-room assignment triggers, retries, MQTT handling, or the room menu, read the room-assignment section in [PRODUCT_INTERACTION_PROFILES.md](../../../../docs/product/PRODUCT_INTERACTION_PROFILES.md#64-房间分配刷新), the group-room sections of [PRODUCT_REQUIREMENTS.md](../../../../docs/product/PRODUCT_REQUIREMENTS.md), and [ROOM_PROTOCOL.md](../../../../docs/product/ROOM_PROTOCOL.md) before editing.

Run `python3 tools/test_contract.py` after changes. Completion requires its room contract to prove that idle time never initiates an assignment request, while an explicit sync or MQTT assignment-change event initiates exactly one request.
