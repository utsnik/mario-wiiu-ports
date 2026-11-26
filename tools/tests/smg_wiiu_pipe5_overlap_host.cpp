#include <cstdio>
#include <cstdlib>

namespace {

enum State {
	Empty,
	Queued,
	WorkerDone,
	CallbackDone,
	Retired,
	Fallback,
};

struct Ticket {
	unsigned sequence;
	State state;
	bool rendered;
	bool viQueued;
	unsigned retireCount;
	unsigned presentCount;
	unsigned fallbackCount;
	bool callbackFails;
	bool workerDelayed;
};

struct Machine {
	Ticket tickets[2];
	unsigned serviceCapacity;
	unsigned serviceUsed;
	bool sceneTransition;

	Machine() : serviceCapacity(2), serviceUsed(0), sceneTransition(false) {
		for (unsigned i = 0; i != 2; ++i) {
			tickets[i] = Ticket{0, Empty, false, false, 0, 0, 0, false, false};
		}
	}

	Ticket& ticket(unsigned sequence) { return tickets[(sequence - 1) % 2]; }

	bool begin(unsigned sequence, bool rendered) {
		Ticket& t = ticket(sequence);
		if (t.state != Empty && t.state != Retired && t.state != Fallback)
			return false;
		t = Ticket{sequence, Empty, rendered, false, 0, 0, 0, false, false};
		if (sceneTransition || serviceUsed == serviceCapacity) {
			fallback(t);
			return false;
		}
		++serviceUsed;
		t.state = Queued;
		return true;
	}

	void fallback(Ticket& t) {
		t.state = Fallback;
		++t.fallbackCount;
	}

	void worker(Ticket& t) {
		if (t.state != Queued || t.workerDelayed) return;
		t.state = WorkerDone;
	}

	void service(Ticket& t) {
		if (t.state != WorkerDone) return;
		if (t.callbackFails) {
			fallback(t);
			return;
		}
		t.state = CallbackDone;
		t.state = Retired;
		++t.retireCount;
		if (t.rendered) ++t.presentCount;
		--serviceUsed;
	}

	void consumeVi(Ticket& t) {
		// Repeated VI/retrace delivery may advance pacing but consumes this owner once.
		if (t.viQueued) return;
		t.viQueued = true;
	}

	void validate(const Ticket& t) const {
		if (t.state == Retired) {
			check(t.retireCount == 1, "accepted ticket must retire exactly once");
			check(t.presentCount == (t.rendered ? 1u : 0u), "accepted ticket present count");
			check(t.fallbackCount == 0, "accepted ticket unexpectedly fell back");
		} else if (t.state == Fallback) {
			check(t.fallbackCount == 1, "rejected ticket must use legacy fallback once");
			check(t.retireCount == 0 && t.presentCount == 0, "fallback ticket claimed overlap retire/present");
		}
	}

private:
	static void check(bool ok, const char* message) {
		if (!ok) {
			std::fprintf(stderr, "pipe5-overlap-host: FAIL: %s\n", message);
			std::exit(1);
		}
	}
};

void check(bool ok, const char* message) {
	if (!ok) {
		std::fprintf(stderr, "pipe5-overlap-host: FAIL: %s\n", message);
		std::exit(1);
	}
}

void test_two_ticket_render_and_nonrender() {
	Machine m;
	check(m.begin(1, true), "rendered ticket admitted");
	check(m.begin(2, false), "non-rendered ticket admitted in second slot");
	Ticket& first = m.ticket(1);
	Ticket& second = m.ticket(2);
	m.worker(first);
	m.service(first);
	m.worker(second);
	m.service(second);
	m.validate(first);
	m.validate(second);
	check(m.serviceUsed == 0, "both tickets drained service ownership");
}

void test_vi_message_and_delayed_worker() {
	Machine m;
	check(m.begin(1, true), "VI ticket admitted");
	Ticket& t = m.ticket(1);
	t.workerDelayed = true;
	m.consumeVi(t);
	m.consumeVi(t);
	check(t.viQueued && t.presentCount == 0, "queued VI message cannot present before worker callback");
	m.worker(t);
	check(t.state == Queued, "delayed worker preserves queued state");
	t.workerDelayed = false;
	m.worker(t);
	m.service(t);
	m.consumeVi(t);
	m.validate(t);
}

void test_callback_failure_and_service_exhaustion() {
	Machine failedCallback;
	check(failedCallback.begin(1, true), "callback failure ticket admitted");
	Ticket& failed = failedCallback.ticket(1);
	failed.callbackFails = true;
	failedCallback.worker(failed);
	failedCallback.service(failed);
	failedCallback.validate(failed);

	Machine full;
	full.serviceCapacity = 1;
	check(full.begin(1, true), "first service-ring ticket admitted");
	check(!full.begin(2, true), "service-ring exhaustion rejects second ticket");
	full.validate(full.ticket(2));
	full.worker(full.ticket(1));
	full.service(full.ticket(1));
	full.validate(full.ticket(1));
}

void test_scene_transition() {
	Machine m;
	m.sceneTransition = true;
	check(!m.begin(1, true), "scene transition rejects overlap ticket");
	m.validate(m.ticket(1));
}

} // namespace

int main() {
	test_two_ticket_render_and_nonrender();
	test_vi_message_and_delayed_worker();
	test_callback_failure_and_service_exhaustion();
	test_scene_transition();
	std::puts("pipe5-overlap-host: PASS (two tickets, rendered/non-rendered, VI, delayed worker, callback failure, ring exhaustion, scene transition)");
	return 0;
}
