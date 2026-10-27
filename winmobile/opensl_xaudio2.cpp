// The OpenSL ES subset in compat/SLES/OpenSLES_Android.h on top of XAudio2,
// so the Android sound code (main/android/digi.c, libmve/mveplay.c) plays
// unchanged. Each audio player is one XAudio2 source voice.

#include <SLES/OpenSLES_Android.h>

#include <xaudio2.h>
#include <atomic>
#include <math.h>

namespace {

const struct SLInterfaceID_ iids[4] = {};

// What an interface pointer points at: the method table and the object it belongs to
template <typename Itf>
struct Slot {
	const Itf *methods;
	void *owner;
};

template <typename T, typename Itf>
T *owner_of(const Itf *const *self) {
	return static_cast<T *>(reinterpret_cast<const Slot<Itf> *>(self)->owner);
}

struct Engine {
	Slot<SLObjectItf_> object;
	Slot<SLEngineItf_> engine;
	IXAudio2 *xaudio = nullptr;
	IXAudio2MasteringVoice *master = nullptr;
	UINT32 master_channels = 2;
};

struct OutputMix {
	Slot<SLObjectItf_> object;
};

struct Player;

class VoiceCallback : public IXAudio2VoiceCallback {
public:
	Player *player = nullptr;

	void STDMETHODCALLTYPE OnBufferEnd(void *context) override;
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
	void STDMETHODCALLTYPE OnStreamEnd() override {}
	void STDMETHODCALLTYPE OnBufferStart(void *) override {}
	void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
	void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}
};

struct Player {
	Slot<SLObjectItf_> object;
	Slot<SLPlayItf_> play;
	Slot<SLVolumeItf_> volume;
	Slot<SLAndroidSimpleBufferQueueItf_> queue;
	Engine *engine;
	WAVEFORMATEX format = {};
	IXAudio2SourceVoice *voice = nullptr;
	VoiceCallback callback;
	SLuint32 state = SL_PLAYSTATE_STOPPED;
	// Buffers from before the last Clear or stop still report their end; the
	// generation in their context tells them apart
	std::atomic<UINT_PTR> generation{0};
	std::atomic<int> pending{0};
	slPlayCallback play_callback = nullptr;
	void *play_context = nullptr;
	SLuint32 event_mask = 0;
};

void VoiceCallback::OnBufferEnd(void *context) {
	if ((UINT_PTR) context != player->generation) {
		return;
	}
	if (--player->pending == 0 && player->play_callback &&
		(player->event_mask & SL_PLAYEVENT_HEADATEND)) {
		player->play_callback(&player->play.methods, player->play_context, SL_PLAYEVENT_HEADATEND);
	}
}

// Drops everything queued; the voice stays stopped
void flush(Player *p) {
	++p->generation;
	p->voice->Stop();
	p->voice->FlushSourceBuffers();
	p->pending = 0;
}

void set_pan(Player *p, float pan) {
	float matrix[16] = {};
	UINT32 in = p->format.nChannels, out = p->engine->master_channels;

	if (in != 1 || out < 2 || out * in > 16) {
		return;
	}
	matrix[0] = pan > 0 ? 1 - pan : 1;	// left
	matrix[1] = pan < 0 ? 1 + pan : 1;	// right
	p->voice->SetOutputMatrix(nullptr, in, out, matrix);
}

// Player methods

SLresult SetPlayState(SLPlayItf self, SLuint32 state) {
	Player *p = owner_of<Player>(self);
	if (!p->voice) {
		return SL_RESULT_RESOURCE_ERROR;
	}
	switch (state) {
		case SL_PLAYSTATE_PLAYING: p->voice->Start(); break;
		case SL_PLAYSTATE_PAUSED: p->voice->Stop(); break;
		case SL_PLAYSTATE_STOPPED: flush(p); break;
		default: return SL_RESULT_PARAMETER_INVALID;
	}
	p->state = state;
	return SL_RESULT_SUCCESS;
}

SLresult GetPlayState(SLPlayItf self, SLuint32 *state) {
	*state = owner_of<Player>(self)->state;
	return SL_RESULT_SUCCESS;
}

SLresult RegisterCallback(SLPlayItf self, slPlayCallback callback, void *context) {
	Player *p = owner_of<Player>(self);
	p->play_callback = callback;
	p->play_context = context;
	return SL_RESULT_SUCCESS;
}

SLresult SetCallbackEventsMask(SLPlayItf self, SLuint32 flags) {
	owner_of<Player>(self)->event_mask = flags;
	return SL_RESULT_SUCCESS;
}

SLresult SetVolumeLevel(SLVolumeItf self, SLmillibel level) {
	Player *p = owner_of<Player>(self);
	if (p->voice) {
		p->voice->SetVolume(level <= -9600 ? 0.0f : powf(10.0f, level / 2000.0f));
	}
	return SL_RESULT_SUCCESS;
}

SLresult EnableStereoPosition(SLVolumeItf self, SLboolean enable) {
	if (!enable) {
		Player *p = owner_of<Player>(self);
		if (p->voice) {
			set_pan(p, 0);
		}
	}
	return SL_RESULT_SUCCESS;
}

SLresult SetStereoPosition(SLVolumeItf self, SLpermille position) {
	Player *p = owner_of<Player>(self);
	if (p->voice) {
		set_pan(p, position / 1000.0f);
	}
	return SL_RESULT_SUCCESS;
}

SLresult Enqueue(SLAndroidSimpleBufferQueueItf self, const void *buffer, SLuint32 size) {
	Player *p = owner_of<Player>(self);
	XAUDIO2_BUFFER b = {};

	if (!p->voice) {
		return SL_RESULT_RESOURCE_ERROR;
	}
	// XAudio2 takes whole sample frames only
	b.AudioBytes = size - size % p->format.nBlockAlign;
	if (b.AudioBytes == 0) {
		return SL_RESULT_SUCCESS;
	}
	b.pAudioData = static_cast<const BYTE *>(buffer);
	b.pContext = (void *) (UINT_PTR) p->generation;
	++p->pending;
	if (FAILED(p->voice->SubmitSourceBuffer(&b))) {
		--p->pending;
		return SL_RESULT_RESOURCE_ERROR;
	}
	return SL_RESULT_SUCCESS;
}

SLresult Clear(SLAndroidSimpleBufferQueueItf self) {
	Player *p = owner_of<Player>(self);
	if (p->voice) {
		flush(p);
		if (p->state == SL_PLAYSTATE_PLAYING) {
			p->voice->Start();
		}
	}
	return SL_RESULT_SUCCESS;
}

const SLPlayItf_ play_methods = {SetPlayState, GetPlayState, RegisterCallback,
								 SetCallbackEventsMask};
const SLVolumeItf_ volume_methods = {SetVolumeLevel, EnableStereoPosition, SetStereoPosition};
const SLAndroidSimpleBufferQueueItf_ queue_methods = {Enqueue, Clear};

SLresult PlayerRealize(SLObjectItf self, SLboolean async) {
	Player *p = owner_of<Player>(self);
	if (!p->engine->xaudio ||
		FAILED(p->engine->xaudio->CreateSourceVoice(&p->voice, &p->format, 0,
													XAUDIO2_DEFAULT_FREQ_RATIO, &p->callback))) {
		p->voice = nullptr;
		return SL_RESULT_RESOURCE_ERROR;
	}
	set_pan(p, 0);
	return SL_RESULT_SUCCESS;
}

SLresult PlayerGetInterface(SLObjectItf self, const SLInterfaceID iid, void *out) {
	Player *p = owner_of<Player>(self);
	if (iid == SL_IID_PLAY) {
		*static_cast<SLPlayItf *>(out) = &p->play.methods;
	} else if (iid == SL_IID_VOLUME) {
		*static_cast<SLVolumeItf *>(out) = &p->volume.methods;
	} else if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) {
		*static_cast<SLAndroidSimpleBufferQueueItf *>(out) = &p->queue.methods;
	} else {
		return SL_RESULT_FEATURE_UNSUPPORTED;
	}
	return SL_RESULT_SUCCESS;
}

// The player is kept as an empty shell rather than freed: digi.c goes on
// using the interfaces of the player mveplay.c destroys for a movie's sound
void PlayerDestroy(SLObjectItf self) {
	Player *p = owner_of<Player>(self);
	if (p->voice) {
		// Waits for callbacks in flight to finish
		p->voice->DestroyVoice();
		p->voice = nullptr;
	}
	p->play_callback = nullptr;
	p->state = SL_PLAYSTATE_STOPPED;
}

const SLObjectItf_ player_object_methods = {PlayerRealize, PlayerGetInterface, PlayerDestroy};

// Output mix: XAudio2's mastering voice does the mixing, nothing to do here

SLresult MixRealize(SLObjectItf self, SLboolean async) {
	return SL_RESULT_SUCCESS;
}

SLresult MixGetInterface(SLObjectItf self, const SLInterfaceID iid, void *out) {
	return SL_RESULT_FEATURE_UNSUPPORTED;
}

void MixDestroy(SLObjectItf self) {
	delete owner_of<OutputMix>(self);
}

const SLObjectItf_ mix_object_methods = {MixRealize, MixGetInterface, MixDestroy};

// Engine

SLresult CreateAudioPlayer(SLEngineItf self, SLObjectItf *out, SLDataSource *source,
						   SLDataSink *sink, SLuint32 count, const SLInterfaceID *ids,
						   const SLboolean *required) {
	Engine *e = owner_of<Engine>(self);
	const SLDataFormat_PCM *pcm = static_cast<const SLDataFormat_PCM *>(source->pFormat);

	if (!pcm || pcm->formatType != SL_DATAFORMAT_PCM) {
		return SL_RESULT_PARAMETER_INVALID;
	}
	Player *p = new Player;
	p->object = {&player_object_methods, p};
	p->play = {&play_methods, p};
	p->volume = {&volume_methods, p};
	p->queue = {&queue_methods, p};
	p->engine = e;
	p->callback.player = p;
	p->format.wFormatTag = WAVE_FORMAT_PCM;
	p->format.nChannels = (WORD) pcm->numChannels;
	p->format.nSamplesPerSec = pcm->samplesPerSec / 1000;
	p->format.wBitsPerSample = (WORD) pcm->bitsPerSample;
	p->format.nBlockAlign = (WORD) (p->format.nChannels * p->format.wBitsPerSample / 8);
	p->format.nAvgBytesPerSec = p->format.nSamplesPerSec * p->format.nBlockAlign;
	*out = &p->object.methods;
	return SL_RESULT_SUCCESS;
}

SLresult CreateOutputMix(SLEngineItf self, SLObjectItf *out, SLuint32 count,
						 const SLInterfaceID *ids, const SLboolean *required) {
	OutputMix *m = new OutputMix;
	m->object = {&mix_object_methods, m};
	*out = &m->object.methods;
	return SL_RESULT_SUCCESS;
}

const SLEngineItf_ engine_methods = {CreateAudioPlayer, CreateOutputMix};

SLresult EngineRealize(SLObjectItf self, SLboolean async) {
	Engine *e = owner_of<Engine>(self);
	XAUDIO2_VOICE_DETAILS details;

	if (FAILED(XAudio2Create(&e->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
		e->xaudio = nullptr;
		return SL_RESULT_RESOURCE_ERROR;
	}
	if (FAILED(e->xaudio->CreateMasteringVoice(&e->master))) {
		e->master = nullptr;
		return SL_RESULT_RESOURCE_ERROR;
	}
	e->master->GetVoiceDetails(&details);
	e->master_channels = details.InputChannels;
	return SL_RESULT_SUCCESS;
}

SLresult EngineGetInterface(SLObjectItf self, const SLInterfaceID iid, void *out) {
	if (iid != SL_IID_ENGINE) {
		return SL_RESULT_FEATURE_UNSUPPORTED;
	}
	*static_cast<SLEngineItf *>(out) = &owner_of<Engine>(self)->engine.methods;
	return SL_RESULT_SUCCESS;
}

void EngineDestroy(SLObjectItf self) {
	Engine *e = owner_of<Engine>(self);
	if (e->master) {
		e->master->DestroyVoice();
	}
	if (e->xaudio) {
		e->xaudio->Release();
	}
	delete e;
}

const SLObjectItf_ engine_object_methods = {EngineRealize, EngineGetInterface, EngineDestroy};

}

extern "C" {

const SLInterfaceID SL_IID_ENGINE = &iids[0];
const SLInterfaceID SL_IID_PLAY = &iids[1];
const SLInterfaceID SL_IID_VOLUME = &iids[2];
const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &iids[3];

SLresult slCreateEngine(SLObjectItf *out, SLuint32 option_count, const void *options,
						SLuint32 count, const SLInterfaceID *ids, const SLboolean *required) {
	Engine *e = new Engine;
	e->object = {&engine_object_methods, e};
	e->engine = {&engine_methods, e};
	*out = &e->object.methods;
	return SL_RESULT_SUCCESS;
}

}
