from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from build import normalize_dependency_lock, prepare_dependency_lock  # noqa: E402


class BuildDispatcherTest(unittest.TestCase):
    def test_prepares_portable_local_paths_for_component_manager(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "firmware" / "board"
            project.mkdir(parents=True)
            lock = project / "dependencies.lock"
            lock.write_text(
                "source:\n  type: local\n  path: $PWD/components/codec\n",
                encoding="utf-8",
            )

            self.assertTrue(prepare_dependency_lock(project))
            self.assertIn(
                f"path: {project.resolve()}/components/codec",
                lock.read_text(encoding="utf-8"),
            )

    def test_normalizes_project_local_component_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "firmware" / "board"
            project.mkdir(parents=True)
            lock = project / "dependencies.lock"
            lock.write_text(
                "source:\n"
                "  type: local\n"
                f"  path: {project.resolve()}/components/codec\n",
                encoding="utf-8",
            )

            self.assertTrue(normalize_dependency_lock(project))
            self.assertEqual(
                lock.read_text(encoding="utf-8"),
                "source:\n  type: local\n  path: $PWD/components/codec\n",
            )
            self.assertFalse(normalize_dependency_lock(project))

    def test_does_not_rewrite_unrelated_absolute_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / "project"
            project.mkdir()
            lock = project / "dependencies.lock"
            lock.write_text("source:\n  path: /opt/vendor/component\n", encoding="utf-8")

            self.assertFalse(normalize_dependency_lock(project))
            self.assertIn("/opt/vendor/component", lock.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
