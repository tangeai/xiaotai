"""Guard the event-driven room-assignment refresh contract in ESP runtime."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"static\s+[^\n]+\s+{name}\s*\([^)]*\)\s*\{{", source)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    start = match.end() - 1
    depth = 0
    for offset, char in enumerate(source[start:], start=start):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise AssertionError(f"unterminated function: {name}")


class RoomAssignmentPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = RUNTIME.read_text(encoding="utf-8")

    def test_assignment_read_is_one_shot(self):
        body = function_body(self.source, "room_request_assignment")
        self.assertIn("s_room_sync_due_ms = 0;", body)
        self.assertNotIn("now_ms() +", body)
        self.assertNotIn("ROOM_SYNC_INTERVAL_MS", self.source)

    def test_media_release_and_http_failure_do_not_schedule_refresh(self):
        stop = function_body(self.source, "room_stop_connection")
        self.assertNotIn("s_room_sync_due_ms", stop)

        http = function_body(self.source, "handle_room_http")
        failure = http[: http.index("if (event->command == ROOM_HTTP_ASSIGNMENT)")]
        self.assertNotIn("s_room_sync_due_ms", failure)

    def test_only_documented_edges_request_assignment(self):
        platform_case = self.source.split("case EVENT_PLATFORM_ONLINE:", 1)[1].split(
            "break;", 1
        )[0]
        self.assertNotIn("s_room_sync_due_ms", platform_case)

        signal = function_body(self.source, "handle_platform_signal")
        changed = signal.split('strcmp(signal, "room_assignment_changed")', 1)[1]
        self.assertIn("s_room_sync_due_ms = now_ms();", changed)

        action = function_body(self.source, "handle_room_action")
        self.assertIn(
            "event->command == ROOM_ACTION_SYNC) { s_room_sync_due_ms = now_ms();",
            action,
        )

    def test_room_closed_clears_local_assignment_without_query(self):
        command = function_body(self.source, "handle_room_command")
        closed = command.split('strcmp(method->valuestring, "room_closed")', 1)[1]
        closed = closed.split("return;", 1)[0]
        self.assertIn("s_room_desired = false;", closed)
        self.assertNotIn("s_room_sync_due_ms", closed)

    def test_suspended_room_resumes_from_cached_assignment_only_when_idle(self):
        stop = function_body(self.source, "room_stop_connection")
        self.assertIn('strcmp(presence, "suspended") == 0', stop)
        self.assertIn("s_room_resume_pending = resume_after_foreground;", stop)
        self.assertNotIn("room_request_assignment", stop)

        runtime = function_body(self.source, "runtime_task")
        resume = runtime.split("if (s_room_resume_pending", 1)[1].split(
            "room_request_token();", 1
        )[0]
        self.assertIn("STARTER_RUNTIME_WAITING", resume)
        self.assertIn("!session_incoming_pending()", resume)
        self.assertIn("s_room_pending_presence_body[0] == '\\0'", resume)
        self.assertNotIn("room_request_assignment", resume)

        token = function_body(self.source, "room_request_token")
        self.assertIn("s_room_resume_pending = false;", token)


if __name__ == "__main__":
    unittest.main()
