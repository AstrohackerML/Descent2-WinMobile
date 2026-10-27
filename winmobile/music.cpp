// Music: the game's HMP songs, converted to standard MIDI by hmp2mid (main/hmp.c),
// played on the Windows General MIDI synthesizer on a thread of their own.
// Takes the place of main/android/hmp.c and the Android MediaPlayer glue.
// Redbook (CD) tracks are not supported, so the game falls back to MIDI.

#include <ppltasks.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

using namespace concurrency;
using namespace Windows::Devices::Midi;

// The core's bool is int, and its headers do not build as C++
typedef struct hmp_file hmp_file;
extern "C" void hmp2mid(hmp_file *hmp, unsigned char **midbuf, unsigned int *midlen);

namespace {

struct Event {
	unsigned long long time_us;
	unsigned char data[3];
};

MidiSynthesizer ^synth;
bool synth_tried;
std::thread player;
std::mutex lock;
std::condition_variable wake;
bool stop_requested;
double volume = 1.0;

unsigned int read_be(const unsigned char *p, int n) {
	unsigned int v = 0;
	for (int i = 0; i < n; ++i) {
		v = v << 8 | p[i];
	}
	return v;
}

bool read_varlen(const unsigned char *&p, const unsigned char *end, unsigned int *out) {
	unsigned int v = 0;
	for (int i = 0; i < 4 && p < end; ++i) {
		unsigned char b = *p++;
		v = v << 7 | (b & 0x7F);
		if (!(b & 0x80)) {
			*out = v;
			return true;
		}
	}
	return false;
}

// Channel messages of a standard MIDI file in play order, with times worked
// out from the tempo changes
std::vector<Event> parse_midi(const unsigned char *mid, unsigned int len) {
	struct TickEvent {
		unsigned int tick;
		unsigned int tempo;	// a tempo change if not 0
		unsigned char data[3];
	};
	std::vector<TickEvent> ticks;
	std::vector<Event> events;

	if (len < 14 || memcmp(mid, "MThd", 4) != 0) {
		return events;
	}
	unsigned int header_len = read_be(mid + 4, 4);
	unsigned int division = read_be(mid + 12, 2);
	if (division == 0 || division & 0x8000) {
		return events;	// SMPTE timing, which hmp2mid never writes
	}

	const unsigned char *p = mid + 8 + header_len, *end = mid + len;
	while (p + 8 <= end) {
		unsigned int chunk_len = read_be(p + 4, 4);
		const unsigned char *track = p + 8;
		const unsigned char *track_end = track + chunk_len < end ? track + chunk_len : end;
		bool is_track = memcmp(p, "MTrk", 4) == 0;
		p = track_end;
		if (!is_track) {
			continue;
		}

		unsigned int tick = 0, delta, n;
		unsigned char status = 0;
		const unsigned char *q = track;
		while (q < track_end && read_varlen(q, track_end, &delta)) {
			tick += delta;
			if (q >= track_end) {
				break;
			}
			if (*q == 0xFF) {	// meta event
				if (q + 2 > track_end) {
					break;
				}
				unsigned char type = q[1];
				q += 2;
				if (!read_varlen(q, track_end, &n) || q + n > track_end) {
					break;
				}
				if (type == 0x51 && n == 3) {
					ticks.push_back({tick, read_be(q, 3), {}});
				}
				q += n;
				if (type == 0x2F) {
					break;
				}
				continue;
			}
			if (*q == 0xF0 || *q == 0xF7) {	// sysex, of no use to the GM synth
				++q;
				if (!read_varlen(q, track_end, &n)) {
					break;
				}
				q += n;
				continue;
			}
			if (*q & 0x80) {
				status = *q++;
			} else if (!status) {
				break;
			}
			int size = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 1 : 2;
			if (q + size > track_end) {
				break;
			}
			TickEvent e = {tick, 0, {status, q[0], size == 2 ? q[1] : (unsigned char) 0}};
			ticks.push_back(e);
			q += size;
		}
	}

	std::stable_sort(ticks.begin(), ticks.end(), [](const TickEvent &a, const TickEvent &b) {
		return a.tick < b.tick;
	});
	unsigned long long time_us = 0;
	unsigned int last_tick = 0, tempo = 500000;
	for (const TickEvent &t : ticks) {
		time_us += (unsigned long long) (t.tick - last_tick) * tempo / division;
		last_tick = t.tick;
		if (t.tempo) {
			tempo = t.tempo;
		} else {
			events.push_back({time_us, {t.data[0], t.data[1], t.data[2]}});
		}
	}
	return events;
}

void send(const unsigned char *d) {
	unsigned char channel = d[0] & 0x0F;
	IMidiMessage ^message;

	switch (d[0] & 0xF0) {
		case 0x80: message = ref new MidiNoteOffMessage(channel, d[1], d[2]); break;
		case 0x90: message = ref new MidiNoteOnMessage(channel, d[1], d[2]); break;
		case 0xA0: message = ref new MidiPolyphonicKeyPressureMessage(channel, d[1], d[2]); break;
		case 0xB0: message = ref new MidiControlChangeMessage(channel, d[1], d[2]); break;
		case 0xC0: message = ref new MidiProgramChangeMessage(channel, d[1]); break;
		case 0xD0: message = ref new MidiChannelPressureMessage(channel, d[1]); break;
		case 0xE0:
			message = ref new MidiPitchBendChangeMessage(channel, (unsigned short) (d[1] | d[2] << 7));
			break;
		default: return;
	}
	synth->SendMessage(message);
}

void silence() {
	for (unsigned char channel = 0; channel < 16; ++channel) {
		synth->SendMessage(ref new MidiControlChangeMessage(channel, 123, 0));	// all notes off
		synth->SendMessage(ref new MidiControlChangeMessage(channel, 121, 0));	// reset controllers
	}
}

void play(std::vector<Event> events, bool loop) {
	using clock = std::chrono::steady_clock;
	clock::time_point start = clock::now();

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	do {
		for (const Event &e : events) {
			std::unique_lock<std::mutex> guard(lock);
			if (wake.wait_until(guard, start + std::chrono::microseconds(e.time_us),
								[] { return stop_requested; })) {
				return;
			}
			guard.unlock();
			try {
				send(e.data);
			} catch (...) {
				// A lost note is better than a crash
			}
		}
		if (events.back().time_us == 0) {
			return;
		}
		start += std::chrono::microseconds(events.back().time_us);
	} while (loop);
}

void stop_player() {
	if (player.joinable()) {
		{
			std::lock_guard<std::mutex> guard(lock);
			stop_requested = true;
		}
		wake.notify_all();
		player.join();
		try {
			silence();
		} catch (...) {
		}
	}
	stop_requested = false;
}

bool have_synth() {
	if (!synth_tried) {
		synth_tried = true;
		try {
			synth = create_task(MidiSynthesizer::CreateAsync()).get();
		} catch (...) {
			synth = nullptr;
		}
		if (synth) {
			synth->Volume = volume;
		}
	}
	return synth != nullptr;
}

}

extern "C" {

void hmp_init() {
}

void hmp_stop(hmp_file *hmp) {
	if (synth) {
		stop_player();
	}
}

void hmp_setvolume(hmp_file *hmp, int vol) {
	volume = vol / 127.0;
	if (synth) {
		synth->Volume = volume;
	}
}

int hmp_play(hmp_file *hmp, int loop) {
	unsigned char *mid;
	unsigned int len;

	if (!have_synth()) {
		return -1;
	}
	stop_player();
	hmp2mid(hmp, &mid, &len);
	std::vector<Event> events = parse_midi(mid, len);
	free(mid);
	if (events.empty()) {
		return -1;
	}
	player = std::thread(play, std::move(events), loop != 0);
	return 0;
}

long getRedbookTrackCount() {
	return 0;
}

void setMusicVolume(float vol) {
	volume = vol;
	if (synth) {
		synth->Volume = volume;
	}
}

int playRedbookTrack(int tracknum, int loop) {
	return 0;
}

void stopMusic() {
	hmp_stop(nullptr);
}

}
