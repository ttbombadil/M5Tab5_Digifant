import unittest


class TouchEdgeGate:
    """Host model of the IRQ-confirmed one-event-per-press contract."""

    def __init__(self, press_guard_ms=350):
        self.down = False
        self.press_guard_ms = press_guard_ms
        self.last_candidate_ms = None
        self.suppressed = 0

    def poll(self, pressed, release_confirmed=False, now_ms=0):
        if release_confirmed:
            self.down = False
        if not pressed:
            return False
        if self.down:
            return False
        self.down = True
        if (self.last_candidate_ms is not None and
                now_ms - self.last_candidate_ms < self.press_guard_ms):
            self.last_candidate_ms = now_ms
            self.suppressed += 1
            return False
        self.last_candidate_ms = now_ms
        return True


class TouchStateTests(unittest.TestCase):
    def test_hold_produces_one_event(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        self.assertFalse(gate.poll(True))
        self.assertFalse(gate.poll(True))

    def test_release_rearms_gate(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        self.assertFalse(gate.poll(False, release_confirmed=True))
        self.assertTrue(gate.poll(True, now_ms=1400))

    def test_idle_does_not_create_events(self):
        gate = TouchEdgeGate()
        self.assertFalse(gate.poll(False))
        self.assertFalse(gate.poll(False))

    def test_state_transition_does_not_duplicate_hold(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        # A display/UI transition must not turn one held finger into repeats.
        self.assertFalse(gate.poll(True))
        self.assertFalse(gate.poll(True))

    def test_transient_empty_touch_read_does_not_rearm_held_finger(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        self.assertFalse(gate.poll(False, release_confirmed=False))
        self.assertFalse(gate.poll(True))

    def test_short_false_release_cannot_forward_touch_to_next_state(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        self.assertFalse(gate.poll(False, release_confirmed=True, now_ms=1150))
        self.assertFalse(gate.poll(True, now_ms=1170))
        self.assertEqual(gate.suppressed, 1)

    def test_suppressed_noise_extends_guard_until_touch_is_quiet(self):
        gate = TouchEdgeGate()
        self.assertTrue(gate.poll(True, now_ms=1000))
        self.assertFalse(gate.poll(False, release_confirmed=True, now_ms=1150))
        self.assertFalse(gate.poll(True, now_ms=1200))
        self.assertFalse(gate.poll(False, release_confirmed=True, now_ms=1350))
        self.assertFalse(gate.poll(True, now_ms=1500))
        self.assertFalse(gate.poll(False, release_confirmed=True, now_ms=1650))
        self.assertTrue(gate.poll(True, now_ms=1900))


if __name__ == "__main__":
    unittest.main()
