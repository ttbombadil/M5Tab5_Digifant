import unittest


class TouchEdgeGate:
    """Host model of the one-event-per-press contract used by the sketch."""

    def __init__(self):
        self.down = False

    def poll(self, pressed):
        if not pressed:
            self.down = False
            return False
        if self.down:
            return False
        self.down = True
        return True


class TouchStateTests(unittest.TestCase):
    def test_hold_produces_one_event(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True))
        self.assertFalse(gate.poll(True))
        self.assertFalse(gate.poll(True))

    def test_release_rearms_gate(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True))
        self.assertFalse(gate.poll(False))
        self.assertTrue(gate.poll(True))

    def test_idle_does_not_create_events(self):
        gate = TouchEdgeGate()
        self.assertFalse(gate.poll(False))
        self.assertFalse(gate.poll(False))

    def test_state_transition_does_not_duplicate_hold(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True))
        # A display/UI transition must not turn one held finger into repeats.
        self.assertFalse(gate.poll(True))
        self.assertFalse(gate.poll(True))


if __name__ == "__main__":
    unittest.main()
