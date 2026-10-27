// The part of OpenSL ES (with the Android buffer queue) that main/android/digi.c
// and libmve/mveplay.c use, played through XAudio2 by opensl_xaudio2.cpp.
// Only the methods those files call are here, so this is not ABI compatible
// with real OpenSL ES.
#ifndef WM_OPENSLES_H
#define WM_OPENSLES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char SLuint8;
typedef short SLint16;
typedef unsigned int SLuint32;
typedef SLuint32 SLboolean;
typedef SLuint32 SLresult;
typedef SLint16 SLmillibel;
typedef SLint16 SLpermille;

#define SLAPIENTRY

#define SL_BOOLEAN_FALSE ((SLboolean) 0)
#define SL_BOOLEAN_TRUE ((SLboolean) 1)

#define SL_RESULT_SUCCESS ((SLresult) 0)
#define SL_RESULT_PARAMETER_INVALID ((SLresult) 2)
#define SL_RESULT_MEMORY_FAILURE ((SLresult) 3)
#define SL_RESULT_RESOURCE_ERROR ((SLresult) 4)
#define SL_RESULT_FEATURE_UNSUPPORTED ((SLresult) 12)

#define SL_PLAYSTATE_STOPPED ((SLuint32) 1)
#define SL_PLAYSTATE_PAUSED ((SLuint32) 2)
#define SL_PLAYSTATE_PLAYING ((SLuint32) 3)

#define SL_PLAYEVENT_HEADATEND ((SLuint32) 1)

#define SL_DATAFORMAT_PCM ((SLuint32) 2)
#define SL_PCMSAMPLEFORMAT_FIXED_8 ((SLuint32) 8)
#define SL_PCMSAMPLEFORMAT_FIXED_16 ((SLuint32) 16)
#define SL_SPEAKER_FRONT_LEFT ((SLuint32) 1)
#define SL_SPEAKER_FRONT_RIGHT ((SLuint32) 2)
#define SL_SPEAKER_FRONT_CENTER ((SLuint32) 4)
#define SL_BYTEORDER_LITTLEENDIAN ((SLuint32) 2)

#define SL_DATALOCATOR_OUTPUTMIX ((SLuint32) 4)
#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE ((SLuint32) 0x800007BD)

typedef const struct SLInterfaceID_ {
	int unused;
} *SLInterfaceID;

extern const SLInterfaceID SL_IID_ENGINE;
extern const SLInterfaceID SL_IID_PLAY;
extern const SLInterfaceID SL_IID_VOLUME;
extern const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE;

struct SLObjectItf_;
struct SLEngineItf_;
struct SLPlayItf_;
struct SLVolumeItf_;
struct SLAndroidSimpleBufferQueueItf_;

typedef const struct SLObjectItf_ *const *SLObjectItf;
typedef const struct SLEngineItf_ *const *SLEngineItf;
typedef const struct SLPlayItf_ *const *SLPlayItf;
typedef const struct SLVolumeItf_ *const *SLVolumeItf;
typedef const struct SLAndroidSimpleBufferQueueItf_ *const *SLAndroidSimpleBufferQueueItf;

typedef struct SLDataFormat_PCM_ {
	SLuint32 formatType;
	SLuint32 numChannels;
	SLuint32 samplesPerSec;	// in milliHertz
	SLuint32 bitsPerSample;
	SLuint32 containerSize;
	SLuint32 channelMask;
	SLuint32 endianness;
} SLDataFormat_PCM;

typedef struct SLDataLocator_AndroidSimpleBufferQueue_ {
	SLuint32 locatorType;
	SLuint32 numBuffers;
} SLDataLocator_AndroidSimpleBufferQueue;

typedef struct SLDataLocator_OutputMix_ {
	SLuint32 locatorType;
	SLObjectItf outputMix;
} SLDataLocator_OutputMix;

typedef struct SLDataSource_ {
	void *pLocator;
	void *pFormat;
} SLDataSource;

typedef struct SLDataSink_ {
	void *pLocator;
	void *pFormat;
} SLDataSink;

typedef void (SLAPIENTRY *slPlayCallback)(SLPlayItf caller, void *pContext, SLuint32 event);

struct SLObjectItf_ {
	SLresult (*Realize)(SLObjectItf self, SLboolean async);
	SLresult (*GetInterface)(SLObjectItf self, const SLInterfaceID iid, void *pInterface);
	void (*Destroy)(SLObjectItf self);
};

struct SLEngineItf_ {
	SLresult (*CreateAudioPlayer)(SLEngineItf self, SLObjectItf *pPlayer,
								  SLDataSource *pAudioSrc, SLDataSink *pAudioSnk,
								  SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
								  const SLboolean *pInterfaceRequired);
	SLresult (*CreateOutputMix)(SLEngineItf self, SLObjectItf *pMix, SLuint32 numInterfaces,
								const SLInterfaceID *pInterfaceIds,
								const SLboolean *pInterfaceRequired);
};

struct SLPlayItf_ {
	SLresult (*SetPlayState)(SLPlayItf self, SLuint32 state);
	SLresult (*GetPlayState)(SLPlayItf self, SLuint32 *pState);
	SLresult (*RegisterCallback)(SLPlayItf self, slPlayCallback callback, void *pContext);
	SLresult (*SetCallbackEventsMask)(SLPlayItf self, SLuint32 eventFlags);
};

struct SLVolumeItf_ {
	SLresult (*SetVolumeLevel)(SLVolumeItf self, SLmillibel level);
	SLresult (*EnableStereoPosition)(SLVolumeItf self, SLboolean enable);
	SLresult (*SetStereoPosition)(SLVolumeItf self, SLpermille stereoPosition);
};

struct SLAndroidSimpleBufferQueueItf_ {
	SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf self, const void *pBuffer, SLuint32 size);
	SLresult (*Clear)(SLAndroidSimpleBufferQueueItf self);
};

SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions, const void *pEngineOptions,
						SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
						const SLboolean *pInterfaceRequired);

#ifdef __cplusplus
}
#endif

#endif
