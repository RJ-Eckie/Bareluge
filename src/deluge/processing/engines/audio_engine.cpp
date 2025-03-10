/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "processing/engines/audio_engine.h"
#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "extern.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/slicer.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_vector.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/audio_output.h"
#include "processing/engines/cv_engine.h"
#include "processing/live/live_input_buffer.h"
#include "processing/metronome/metronome.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "task_scheduler.h"
#include "timers_interrupts/timers_interrupts.h"
#include "util/functions.h"
#include "util/misc.h"
#include <cstring>
#include <new>

namespace params = deluge::modulation::params;

#if AUTOMATED_TESTER_ENABLED
#include "testing/automated_tester.h"
#endif

extern "C" {
#include "RZA1/mtu/mtu.h"
#include "drivers/ssi/ssi.h"

#include "RZA1/intc/devdrv_intc.h"
// void *__dso_handle = NULL; // This fixes an insane error.
}

#define DISABLE_INTERRUPTS_COUNT (sizeof(disableInterrupts) / sizeof(uint32_t))
uint32_t disableInterrupts[] = {INTC_ID_SPRI0,
                                INTC_ID_DMAINT0 + PIC_TX_DMA_CHANNEL,
                                IRQ_INTERRUPT_0 + 6,
                                INTC_ID_USBI0,
                                INTC_ID_SDHI1_0,
                                INTC_ID_SDHI1_3,
                                INTC_ID_DMAINT0 + OLED_SPI_DMA_CHANNEL,
                                INTC_ID_DMAINT0 + MIDI_TX_DMA_CHANNEL,
                                INTC_ID_SDHI1_1};

using namespace deluge;

extern bool inSpamMode;
extern bool anythingProbablyPressed;
extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

// #define REPORT_CPU_USAGE 1

#define NUM_SAMPLES_FOR_CPU_USAGE_REPORT 32

#define AUDIO_OUTPUT_GAIN_DOUBLINGS 8

#ifdef REPORT_CPU_USAGE
#define REPORT_AVERAGE_NUM 10
int32_t usageTimes[REPORT_AVERAGE_NUM];
#endif

extern "C" uint32_t getAudioSampleTimerMS() {
	return AudioEngine::audioSampleTimer / 44.1;
}
// Pitch, Y, Pressure
int16_t zeroMPEValues[kNumExpressionDimensions] = {0, 0, 0};

namespace AudioEngine {
// statically allocate some of these to reduce allocations
constexpr int32_t kNumVoicesStatic = 48;
constexpr int32_t kNumVoiceSamplesStatic = 48;
constexpr int32_t kNumTimeStretchersStatic = 48;

// used for culling. This can be high now since it's decoupled from the time between renders, and
// will spill into a second render and output if needed so as long as we always render 128 samples in under 128/44100
// seconds it will work maybe this should be user configurable? Values from 60-100 all seem justifiable. 100
// occasionally crackles but means more voices. 60 keeps the modulation updating at about 800hz and LFO2 goes up to
// 100ish
constexpr int32_t numSamplesLimit = 80;
// used for decisions in rendering engine
constexpr int32_t direnessThreshold = numSamplesLimit - 30;

// 7 can overwhelm SD bandwidth if we schedule the loads badly. It could be improved by starting future loads
// earlier for now we provide an outlet in culling a single voice if we're under MIN_VOICES and still getting close
// to the limit
constexpr int MIN_VOICES = 7;

dsp::Reverb reverb{};
PLACE_INTERNAL_FRUNK SideChain reverbSidechain{};
int32_t reverbSidechainVolume;
int32_t reverbSidechainShape;
int32_t reverbPan = 0;

int32_t reverbSidechainVolumeInEffect; // Active right now - possibly overridden by the sound with the most reverb
int32_t reverbSidechainShapeInEffect;

bool mustUpdateReverbParamsBeforeNextRender = false;

int32_t sideChainHitPending = false;

uint32_t timeLastSideChainHit = 2147483648;
int32_t sizeLastSideChainHit;

Metronome metronome{};
StereoFloatSample approxRMSLevel{0};
AbsValueFollower envelopeFollower{};
int32_t timeLastPopup{0};

SoundDrum* sampleForPreview;
ParamManagerForTimeline* paramManagerForSamplePreview;

char paramManagerForSamplePreviewMemory[sizeof(ParamManagerForTimeline)];
char sampleForPreviewMemory[sizeof(SoundDrum)];

SampleRecorder* firstRecorder = NULL;

// Let's keep these grouped - the stuff we're gonna access regularly during audio rendering
int32_t cpuDireness = 0;
uint32_t timeDirenessChanged;
uint32_t timeThereWasLastSomeReverb = 0x8FFFFFFF;
int32_t numSamplesLastTime = 0;
uint32_t nextVoiceState = 1;
bool renderInStereo = true;
bool bypassCulling = false;
bool audioRoutineLocked = false;
uint32_t audioSampleTimer = 0;
uint32_t i2sTXBufferPos;
uint32_t i2sRXBufferPos;

bool headphonesPluggedIn;
bool micPluggedIn;
bool lineInPluggedIn;
InputMonitoringMode inputMonitoringMode = InputMonitoringMode::SMART;
bool routineBeenCalled;
uint8_t numHopsEndedThisRoutineCall;

VoiceVector activeVoices{};

LiveInputBuffer* liveInputBuffers[3];

// For debugging
uint16_t lastRoutineTime;

std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> renderingBuffer __attribute__((aligned(CACHE_LINE_SIZE)));
std::array<int32_t, 2 * SSI_TX_BUFFER_NUM_SAMPLES> reverbBuffer __attribute__((aligned(CACHE_LINE_SIZE)));

StereoSample* renderingBufferOutputPos = renderingBuffer.begin();
StereoSample* renderingBufferOutputEnd = renderingBuffer.begin();

int32_t masterVolumeAdjustmentL;
int32_t masterVolumeAdjustmentR;

bool doMonitoring;
MonitoringAction monitoringAction;

uint32_t saddr;

std::array<VoiceSample, kNumVoiceSamplesStatic> voiceSamples{};
VoiceSample* firstUnassignedVoiceSample = voiceSamples.begin();

std::array<TimeStretcher, kNumTimeStretchersStatic> timeStretchers{};
TimeStretcher* firstUnassignedTimeStretcher = timeStretchers.begin();
std::array<Voice, kNumVoicesStatic> staticVoices{};
Voice* firstUnassignedVoice = staticVoices.begin();

// You must set up dynamic memory allocation before calling this, because of its call to setupWithPatching()
void init() {
	paramManagerForSamplePreview = new ((void*)paramManagerForSamplePreviewMemory) ParamManagerForTimeline();
	paramManagerForSamplePreview->setupWithPatching(); // Shouldn't be an error at init time...
	Sound::initParams(paramManagerForSamplePreview);
	sampleForPreview = new ((void*)sampleForPreviewMemory) SoundDrum();
	sampleForPreview->setupAsSample(paramManagerForSamplePreview);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addTimelineCounter(NULL)
	        ->addOtherTwoThingsButNoNoteRow(sampleForPreview, paramManagerForSamplePreview)
	        ->addParamCollectionSummary(paramManagerForSamplePreview->getPatchCableSetSummary());

	((PatchCableSet*)modelStackWithParamCollection->paramCollection)->setupPatching(modelStackWithParamCollection);
	sampleForPreview->patcher.performInitialPatching(sampleForPreview, paramManagerForSamplePreview);

	sampleForPreview->sideChainSendLevel = 2147483647;

	for (int32_t i = 0; i < kNumVoiceSamplesStatic; i++) {
		voiceSamples[i].nextUnassigned = (i == kNumVoiceSamplesStatic - 1) ? NULL : &voiceSamples[i + 1];
	}

	for (int32_t i = 0; i < kNumTimeStretchersStatic; i++) {
		timeStretchers[i].nextUnassigned = (i == kNumTimeStretchersStatic - 1) ? NULL : &timeStretchers[i + 1];
	}
	for (int32_t i = 0; i < kNumVoicesStatic; i++) {
		staticVoices[i].nextUnassigned = (i == kNumVoicesStatic - 1) ? NULL : &staticVoices[i + 1];
	}

	i2sTXBufferPos = (uint32_t)getTxBufferStart();

	i2sRXBufferPos = (uint32_t)getRxBufferStart()
	                 + ((SSI_RX_BUFFER_NUM_SAMPLES - SSI_TX_BUFFER_NUM_SAMPLES - 16)
	                    << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE)); // Subtracting 5 or more seems fine
}

void unassignAllVoices(bool deletingSong) {

	for (int32_t v = 0; v < activeVoices.getNumElements(); v++) {
		Voice* thisVoice = activeVoices.getVoice(v);
		unassignVoice(thisVoice, thisVoice->assignedToSound, nullptr, false);
	}
	activeVoices.empty();

	// Because we unfortunately don't have a master list of VoiceSamples or actively sounding AudioClips,
	// we have to unassign all of those by going through all AudioOutputs.
	// But if there's no currentSong, that's fine - it's already been deleted, and this has already been called for it
	// before then.
	if (currentSong) {
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			if (output->type == OutputType::AUDIO) {
				((AudioOutput*)output)->cutAllSound();
			}
		}
	}
}

void songSwapAboutToHappen() {
	// Otherwise, a timer might get called and try to access Clips that we may have deleted below
	uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);
	logAction("a1");
	currentSong->deleteSoundsWhichWontSound();
	logAction("a2");
	playbackHandler.stopAnyRecording();
}

enum CullType { HARD, FORCE, SOFT_ALWAYS, SOFT };

#define AUDIO_LOG_SIZE 64
bool definitelyLog = false;
#if DO_AUDIO_LOG

uint16_t audioLogTimes[AUDIO_LOG_SIZE];
char audioLogStrings[AUDIO_LOG_SIZE][64];
int32_t numAudioLogItems = 0;
#endif

// To be called when CPU is overloaded and we need to free it up. This stops the voice which has been
// releasing longest, or if none, the voice playing longest.
Voice* cullVoice(bool saveVoice, CullType type, size_t numSamples, Sound* stopFrom) {
	// Only include audio if doing a hard cull and not saving the voice
	bool includeAudio = !saveVoice && type == HARD;
	// Skip releasing voices if doing a soft cull and definitely culling
	bool skipReleasing = (type == SOFT_ALWAYS);
	uint32_t bestRating = 0;
	Voice* bestVoice = NULL;
	for (int32_t v = 0; v < activeVoices.getNumElements(); v++) {
		Voice* thisVoice = activeVoices.getVoice(v);

		uint32_t ratingThisVoice = thisVoice->getPriorityRating();

		if (ratingThisVoice > bestRating) {
			// if we're not skipping releasing voices, or if we are and this one isn't in fast release
			if (!skipReleasing
			    || (thisVoice->envelopes[0].state <= EnvelopeStage::FAST_RELEASE
			        && thisVoice->envelopes[0].fastReleaseIncrement < SOFT_CULL_INCREMENT)) {
				if (stopFrom == nullptr || thisVoice->assignedToSound == stopFrom) {
					bestRating = ratingThisVoice;
					bestVoice = thisVoice;
				}
			}
		}
	}

	if (bestVoice) {
		activeVoices.checkVoiceExists(
		    bestVoice, bestVoice->assignedToSound,
		    "E196"); // ronronsen got!!
		             // https://forums.synthstrom.com/discussion/4097/beta-4-0-0-beta-1-e196-by-loading-wavetable-osc#latest

		switch (type) {
		case SOFT_ALWAYS:
		case SOFT: {
			if (bestVoice->envelopes[0].state < EnvelopeStage::FAST_RELEASE
			    || bestVoice->envelopes[0].fastReleaseIncrement < SOFT_CULL_INCREMENT) {
				bool stillGoing = bestVoice->doFastRelease(SOFT_CULL_INCREMENT);

				if (!stillGoing) {
					unassignVoice(bestVoice, bestVoice->assignedToSound);
				}

#if ALPHA_OR_BETA_VERSION
				D_PRINTLN("soft-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", numSamples,
				          getNumVoices(), getNumAudio());
#if DO_AUDIO_LOG
				dumpAudioLog();
#endif
#endif
			}
			break;
		}

		case FORCE: {
			bool stillGoing = bestVoice->doImmediateRelease();

			if (!stillGoing) {
				unassignVoice(bestVoice, bestVoice->assignedToSound);
			}
#if ALPHA_OR_BETA_VERSION
			D_PRINTLN("force-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", numSamples,
			          getNumVoices(), getNumAudio());
#endif
			break;
		}
		case HARD:
			unassignVoice(bestVoice, bestVoice->assignedToSound, NULL, true, !saveVoice);
			D_PRINTLN("hard-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", numSamples,
			          getNumVoices(), getNumAudio());
		}
	}

	// Or if no Voices to cull, and we're not culling to make a new voice, try culling an AudioClip...
	else if (includeAudio) {
		if (currentSong) {
			currentSong->cullAudioClipVoice();
		}
	}

	return bestVoice;
}
int32_t getNumAudio() {
	return currentSong ? currentSong->countAudioClips() : 0;
}
int32_t getNumVoices() {
	return activeVoices.getNumElements();
}

void routineWithClusterLoading(bool mayProcessUserActionsBetween) {
	logAction("AudioDriver::routineWithClusterLoading");

	routineBeenCalled = false;
	audioFileManager.loadAnyEnqueuedClusters(128, mayProcessUserActionsBetween);
	if (!routineBeenCalled) {
		bypassCulling = true; // yolo?
		logAction("from routineWithClusterLoading()");
		routine(); // -----------------------------------
	}
}

#define TICK_TYPE_SWUNG 1
#define TICK_TYPE_TIMER 2

extern uint16_t g_usb_usbmode;

#if JFTRACE
Debug::AverageDT aeCtr("audio", Debug::mS);
Debug::AverageDT rvb("reverb", Debug::uS);
#endif

uint8_t numRoutines = 0;

// not in header (private to audio engine)
/// determines how many voices to cull based on num audio samples, current voices and numSamplesLimit
inline void cullVoices(size_t numSamples, int32_t numAudio, int32_t numVoice) {
	bool culled = false;
	int32_t numToCull = 0;
	if (numAudio + numVoice > MIN_VOICES) {

		int32_t numSamplesOverLimit = numSamples - numSamplesLimit;
		// If it's real dire, do a proper immediate cull
		if (numSamplesOverLimit >= 20) {

			numToCull = (numSamplesOverLimit >> 3);

			// leave at least 7 - below this point culling won't save us
			// if they can't load their sample in time they'll stop the same way anyway
			numToCull = std::min(numToCull, numAudio + numVoice - MIN_VOICES);
			for (int32_t i = 0; i < numToCull; i++) {
				// hard cull (no release)
				cullVoice(false, FORCE, numSamples, nullptr);
			}
			cullVoice(false, SOFT_ALWAYS, numSamples, nullptr);
#if ALPHA_OR_BETA_VERSION

			definitelyLog = true;
			logAction("hard cull");

#endif
			culled = true;
		}

		// Or if it's just a little bit dire, do a soft cull with fade-out, but only cull for sure if numSamples
		// is increasing
		else if (numSamplesOverLimit >= 0) {

			// If not in first routine call this is inaccurate, so just release another voice since things are
			// probably bad
			cullVoice(false, numRoutines == 0 ? SOFT : SOFT_ALWAYS, numSamples, nullptr);
			logAction("soft cull");
			if (numRoutines > 0) {
				culled = true;
				D_PRINTLN("culling in second routine");
			}
		}
	}
	else {
		int32_t numSamplesOverLimit = numSamples - numSamplesLimit;
		// Cull anyway if things are bad
		if (numSamplesOverLimit >= 40) {
			D_PRINTLN("under min voices but culling anyway");
			cullVoice(false, SOFT, numSamples, nullptr);
			culled = true;
		}
	}
	// blink LED to alert the user
	if (culled && FlashStorage::highCPUUsageIndicator && false) {
		if (indicator_leds::getLedBlinkerIndex(IndicatorLED::PLAY) == 255) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::PLAY);
		}
	}
}

// not in header (private to audio engine)
/// set the direness level and cull any voices
inline void setDireness(size_t numSamples) { // Consider direness and culling - before increasing the number of samples
	// number of samples it took to do the last render
	auto dspTime = (int32_t)(getLastRunTimeforCurrentTask() * 44100.);
	size_t nonDSP = numSamples - dspTime;
	// we don't care about the number that were rendered in the last go, only the ones taken by the first routine call
	numSamples = std::max<int32_t>(dspTime - (int32_t)(numRoutines * numSamples), 0);

	// don't smooth this - used for other decisions as well
	if (numSamples >= direnessThreshold) {

		int32_t newDireness = numSamples - (direnessThreshold - 1);
		if (newDireness > 14) {
			newDireness = 14;
		}

		if (newDireness >= cpuDireness) {
			cpuDireness = newDireness;
			timeDirenessChanged = audioSampleTimer;
		}
		auto numAudio = currentSong ? currentSong->countAudioClips() : 0;
		auto numVoice = getNumVoices();
		if (!bypassCulling) {
			cullVoices(numSamples, numAudio, numVoice);
		}
		else {

			size_t numSamplesOverLimit = numSamples - numSamplesLimit;
			if (numSamplesOverLimit >= 0) {
#if DO_AUDIO_LOG
				definitelyLog = true;
#endif
				D_PRINTLN("numSamples %d, numVoice %d, numAudio %d", numSamples, numVoice, numAudio);
				logAction("skipped cull");
			}
		}
	}
	// otherwise lower it (with some hysteresis to avoid jittering
	else if (numSamples < direnessThreshold - 3) {

		if ((int32_t)(audioSampleTimer - timeDirenessChanged) >= (kSampleRate >> 3)) { // Only if it's been long enough
			timeDirenessChanged = audioSampleTimer;
			cpuDireness--;
			if (cpuDireness < 0) {
				cpuDireness = 0;
			}
			else {
				D_PRINTLN("direness:  %d", cpuDireness);
			}
		}
	}
}

void scheduleMidiGateOutISR(uint32_t saddrPosAtStart, int32_t unadjustedNumSamplesBeforeLappingPlayHead,
                            int32_t timeWithinWindowAtWhichMIDIOrGateOccurs);
void setMonitoringMode();
void renderSongFX(size_t numSamples);
void renderSamplePreview(size_t numSamples);
void renderReverb(size_t numSamples);
void tickSongFinalizeWindows(size_t& numSamples, int32_t& timeWithinWindowAtWhichMIDIOrGateOccurs);
void flushMIDIGateBuffers();
void renderAudio(size_t numSamples);
void renderAudioForStemExport(size_t numSamples);
/// inner loop of audio rendering, deliberately not in header
[[gnu::hot]] void routine_() {
#if JFTRACE
	aeCtr.note();
#endif

#ifndef USE_TASK_MANAGER
	playbackHandler.routine();
#endif
	// At this point, there may be MIDI, including clocks, waiting to be sent.

	GeneralMemoryAllocator::get().checkStack("AudioDriver::routine");

	saddr = (uint32_t)(getTxBufferCurrentPlace());
	uint32_t saddrPosAtStart = saddr >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE);
	size_t numSamples = ((uint32_t)(saddr - i2sTXBufferPos) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
	                    & (SSI_TX_BUFFER_NUM_SAMPLES - 1);

	if (numSamples <= (10 * numRoutines)) {
		if (!numRoutines) {
			ignoreForStats();
		}
		return;
	}
#if AUTOMATED_TESTER_ENABLED
	AutomatedTester::possiblyDoSomething();
#endif
	flushMIDIGateBuffers();

	setDireness(numSamples);

	// Double the number of samples we're going to do - within some constraints
	int32_t sampleThreshold = 6; // If too low, it'll lead to bigger audio windows and stuff
	constexpr size_t maxAdjustedNumSamples = SSI_TX_BUFFER_NUM_SAMPLES;

	int32_t unadjustedNumSamplesBeforeLappingPlayHead = numSamples;

	if (numSamples < maxAdjustedNumSamples) {
		int32_t samplesOverThreshold = numSamples - sampleThreshold;
		if (samplesOverThreshold > 0) {
			samplesOverThreshold = samplesOverThreshold << 1;
			numSamples = sampleThreshold + samplesOverThreshold;
			numSamples = std::min(numSamples, maxAdjustedNumSamples);
		}
	}

	// Want to round to be doing a multiple of 4 samples, so the NEON functions can be utilized most efficiently.
	// Note - this can take numSamples up as high as SSI_TX_BUFFER_NUM_SAMPLES (currently 128).
	if (numSamples >= 3) {
		numSamples = (numSamples + 2) & ~3;
	}

	int32_t timeWithinWindowAtWhichMIDIOrGateOccurs;
	tickSongFinalizeWindows(numSamples, timeWithinWindowAtWhichMIDIOrGateOccurs);

	numSamplesLastTime = numSamples;
	renderAudio(numSamples);

	scheduleMidiGateOutISR(saddrPosAtStart, unadjustedNumSamplesBeforeLappingPlayHead,
	                       timeWithinWindowAtWhichMIDIOrGateOccurs);

#if DO_AUDIO_LOG
	dumpAudioLog();
#endif

	sideChainHitPending = 0;
	audioSampleTimer += numSamples;

	bypassCulling = false;
}
void renderAudio(size_t numSamples) {
	memset(&renderingBuffer, 0, numSamples * sizeof(StereoSample));
	memset(&reverbBuffer, 0, numSamples * sizeof(StereoSample));

	if (sideChainHitPending) {
		timeLastSideChainHit = audioSampleTimer;
		sizeLastSideChainHit = sideChainHitPending;
	}

	numHopsEndedThisRoutineCall = 0;

	// Render audio for song
	if (currentSong) {

		currentSong->renderAudio(renderingBuffer.data(), numSamples, reverbBuffer.data(), sideChainHitPending);
	}

	renderReverb(numSamples);

	renderSamplePreview(numSamples);

	renderSongFX(numSamples);

	metronome.render(renderingBuffer.data(), numSamples);

	approxRMSLevel = envelopeFollower.calcApproxRMS(renderingBuffer.data(), numSamples);

	setMonitoringMode();

	renderingBufferOutputPos = renderingBuffer.begin();
	renderingBufferOutputEnd = renderingBuffer.begin() + numSamples;
}

void renderAudioForStemExport(size_t numSamples) {
	memset(&renderingBuffer, 0, numSamples * sizeof(StereoSample));
	memset(&reverbBuffer, 0, numSamples * sizeof(StereoSample));

	if (sideChainHitPending) {
		timeLastSideChainHit = audioSampleTimer;
		sizeLastSideChainHit = sideChainHitPending;
	}

	numHopsEndedThisRoutineCall = 0;

	// Render audio for song
	if (currentSong) {

		currentSong->renderAudio(renderingBuffer.data(), numSamples, reverbBuffer.data(), sideChainHitPending);
	}

	if (stemExport.includeSongFX) {
		renderReverb(numSamples);
		renderSongFX(numSamples);
	}

	// If we're recording final output for offline stem export with song FX
	// Check if we have a recorder
	SampleRecorder* recorder = audioRecorder.recorder;
	if (recorder && recorder->mode == AudioInputChannel::OFFLINE_OUTPUT) {
		// continue feeding audio if we're not finished recording
		if (recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			recorder->feedAudio((int32_t*)renderingBuffer.data(), numSamples, true);
		}
	}

	approxRMSLevel = envelopeFollower.calcApproxRMS(renderingBuffer.data(), numSamples);

	doMonitoring = false;
	monitoringAction = MonitoringAction::NONE;

	renderingBufferOutputPos = renderingBuffer.begin();
	renderingBufferOutputEnd = renderingBuffer.begin() + numSamples;
}

void flushMIDIGateBuffers() { // Flush everything out of the MIDI buffer now. At this stage, it would only really have
	                          // live user-triggered
	// output and MIDI THRU in it. We want any messages like "start" to go out before we send any clocks below, and
	// also want to give them a head-start being sent and out of the way so the clock messages can be sent on-time
	bool anythingInMidiOutputBufferNow = midiEngine.anythingInOutputBuffer();
	bool anythingInGateOutputBufferNow = cvEngine.isAnythingButRunPending();
	if (anythingInMidiOutputBufferNow || anythingInGateOutputBufferNow) {

		// We're only allowed to do this if the timer ISR isn't pending (i.e. we haven't enabled to timer to trigger
		// it)
		// - otherwise this will all get called soon anyway. I thiiiink this is 100% immune to any synchronization
		// problems?
		if (!isTimerEnabled(TIMER_MIDI_GATE_OUTPUT)) {
			if (anythingInGateOutputBufferNow) {
				cvEngine.updateGateOutputs();
			}
			if (anythingInMidiOutputBufferNow) {
				midiEngine.flushMIDI();
			}
		}
	}
}
void tickSongFinalizeWindows(size_t& numSamples, int32_t& timeWithinWindowAtWhichMIDIOrGateOccurs) {
	timeWithinWindowAtWhichMIDIOrGateOccurs = -1; // -1 means none

	// If a timer-tick is due during or directly after this window of audio samples...
	if (playbackHandler.isEitherClockActive()) {

startAgain:
		int32_t nextTickType = 0;
		uint32_t timeNextTick = audioSampleTimer + 9999;

		if (playbackHandler.isInternalClockActive()) {
			timeNextTick = playbackHandler.timeNextTimerTickBig >> 32;
			nextTickType = TICK_TYPE_TIMER;
		}

		// Or if a swung tick is scheduled sooner...
		if (playbackHandler.swungTickScheduled
		    && (int32_t)(playbackHandler.scheduledSwungTickTime - timeNextTick) < 0) {
			timeNextTick = playbackHandler.scheduledSwungTickTime;
			nextTickType = TICK_TYPE_SWUNG;
		}

		int32_t timeTilNextTick = timeNextTick - audioSampleTimer;

		// If tick occurs right now at the start of this window - which is fairly likely, cos we would have cut the
		// previous window short if it had such a tick during it...
		if (timeTilNextTick <= 0) {
			if (nextTickType == TICK_TYPE_TIMER) {
				playbackHandler.actionTimerTick();
			}
			else if (nextTickType == TICK_TYPE_SWUNG) {
				playbackHandler.actionSwungTick();
				playbackHandler.scheduleSwungTick(); // If that swung tick action just did a song swap, this will have
				                                     // already been called, but fortunately this doesn't break anything
			}

			// Those could have outputted clock or other MIDI / gate
			if (midiEngine.anythingInOutputBuffer() || cvEngine.isAnythingButRunPending()) {
				timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
			}

			goto startAgain;
		}

		// If the tick is during this window, shorten the window so we stop right at the tick
		if (timeTilNextTick < numSamples) {
			numSamples = timeTilNextTick;
		}

		// And now we know how long the window's definitely going to be, see if we want to do any trigger clock or
		// MIDI clock out ticks during it
		if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
			if (playbackHandler.triggerClockOutTickScheduled) {
				int32_t timeTilTriggerClockOutTick = playbackHandler.timeNextTriggerClockOutTick - audioSampleTimer;
				if (timeTilTriggerClockOutTick < numSamples) {
					playbackHandler.doTriggerClockOutTick();
					playbackHandler.scheduleTriggerClockOutTick(); // Schedules another one

					if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
						timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilTriggerClockOutTick;
					}
				}
			}

			if (playbackHandler.midiClockOutTickScheduled) {
				int32_t timeTilMIDIClockOutTick = playbackHandler.timeNextMIDIClockOutTick - audioSampleTimer;
				if (timeTilMIDIClockOutTick < numSamples) {
					playbackHandler.doMIDIClockOutTick();
					playbackHandler.scheduleMIDIClockOutTick(); // Schedules another one

					if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
						timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilMIDIClockOutTick;
					}
				}
			}
		}
	}
}
void renderReverb(size_t numSamples) {
	if (currentSong && mustUpdateReverbParamsBeforeNextRender) {
		updateReverbParams();
		mustUpdateReverbParamsBeforeNextRender = false;
	}

	// Render the reverb sidechain
	int32_t sidechainOutput = 0;
	if (reverbSidechainVolumeInEffect != 0) {
		if (sideChainHitPending != 0) {
			reverbSidechain.registerHit(sideChainHitPending);
		}
#if JFTRACE
		rvb.begin();
#endif

		sidechainOutput = reverbSidechain.render(numSamples, reverbSidechainShapeInEffect);
#if JFTRACE
		rvb.note();
#endif
	}

	int32_t reverbAmplitudeL;
	int32_t reverbAmplitudeR;

	// Stop reverb after 12 seconds of inactivity
	bool reverbOn = ((uint32_t)(audioSampleTimer - timeThereWasLastSomeReverb) < kSampleRate * 12);
	// around the mutable noise floor, ~70dB from peak
	reverbOn |= (std::max(approxRMSLevel.l, approxRMSLevel.r) > 9);

	if (reverbOn) {
		// Patch that to reverb volume
		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(sidechainOutput, reverbSidechainVolumeInEffect) + 0x20000000;
		int32_t reverbOutputVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);

		// Reverb panning
		bool thisDoPanning = (renderInStereo && shouldDoPanning(reverbPan, &reverbAmplitudeL, &reverbAmplitudeR));

		if (thisDoPanning) {
			reverbAmplitudeL = multiply_32x32_rshift32(reverbAmplitudeL, reverbOutputVolume) << 2;
			reverbAmplitudeR = multiply_32x32_rshift32(reverbAmplitudeR, reverbOutputVolume) << 2;
		}
		else {
			reverbAmplitudeL = reverbAmplitudeR = reverbOutputVolume;
		}

		auto reverb_buffer_slice = std::span{reverbBuffer.data(), numSamples};
		auto render_buffer_slice = std::span{renderingBuffer.data(), numSamples};

		// Mix reverb into main render
		reverb.setPanLevels(reverbAmplitudeL, reverbAmplitudeR);
		reverb.process(reverb_buffer_slice, render_buffer_slice);
		logAction("Reverb complete");
	}
}
void renderSamplePreview(size_t numSamples) { // Previewing sample
	if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &gui::context_menu::sample_browser::kit
	    || getCurrentUI() == &gui::context_menu::sample_browser::synth || getCurrentUI() == &slicer) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);

		sampleForPreview->render(modelStack, renderingBuffer.data(), numSamples, reverbBuffer.data(),
		                         sideChainHitPending);
	}
}
void renderSongFX(size_t numSamples) { // LPF and stutter for song (must happen after reverb mixed in, which is why it's
	                                   // happening all the way out here
	masterVolumeAdjustmentL = 167763968; // getParamNeutralValue(params::GLOBAL_VOLUME_POST_FX);
	masterVolumeAdjustmentR = 167763968; // getParamNeutralValue(params::GLOBAL_VOLUME_POST_FX);
	// 167763968 is 134217728 made a bit bigger so that default filter resonance doesn't reduce volume overall

	if (currentSong) {
		currentSong->globalEffectable.setupFilterSetConfig(&masterVolumeAdjustmentL, &currentSong->paramManager);
		currentSong->globalEffectable.processFilters(renderingBuffer.data(), numSamples);
		currentSong->globalEffectable.processSRRAndBitcrushing(renderingBuffer.data(), numSamples,
		                                                       &masterVolumeAdjustmentL, &currentSong->paramManager);

		masterVolumeAdjustmentR = masterVolumeAdjustmentL; // This might have changed in the above function calls

		currentSong->globalEffectable.processStutter(renderingBuffer.data(), numSamples, &currentSong->paramManager);

		// And we do panning for song here too - must be post reverb, and we had to do a volume adjustment below
		// anyway
		int32_t pan = currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_PAN) >> 1;

		if (pan != 0) {
			// Set up panning
			int32_t amplitudeL;
			int32_t amplitudeR;
			bool doPanning = (renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));

			if (doPanning) {
				masterVolumeAdjustmentL = multiply_32x32_rshift32(masterVolumeAdjustmentL, amplitudeL) << 2;
				masterVolumeAdjustmentR = multiply_32x32_rshift32(masterVolumeAdjustmentR, amplitudeR) << 2;
			}
		}
		logAction("mastercomp start");

		int32_t songVolume =
		    getFinalParameterValueVolume(
		        134217728, cableToLinearParamShortcut(
		                       currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME)))
		    >> 1;
		// there used to be a static subtraction of 2 nepers (natural log based dB), this is the multiplicative
		// equivalent
		currentSong->globalEffectable.compressor.render(renderingBuffer.data(), numSamples,
		                                                masterVolumeAdjustmentL >> 1, masterVolumeAdjustmentR >> 1,
		                                                songVolume >> 3);
		masterVolumeAdjustmentL = ONE_Q31;
		masterVolumeAdjustmentR = ONE_Q31;
		logAction("mastercomp end");
	}
}
void setMonitoringMode() { // Monitoring setup
	doMonitoring = false;
	if (audioRecorder.recordingSource == AudioInputChannel::STEREO
	    || audioRecorder.recordingSource == AudioInputChannel::LEFT) {
		if (inputMonitoringMode == InputMonitoringMode::SMART) {
			doMonitoring = (lineInPluggedIn || headphonesPluggedIn);
		}
		else {
			doMonitoring = (inputMonitoringMode == InputMonitoringMode::ON);
		}
	}

	monitoringAction = MonitoringAction::NONE;
	if (doMonitoring && audioRecorder.recorder) { // Double-check
		if (lineInPluggedIn) {                    // Line input
			if (audioRecorder.recorder->inputLooksDifferential()) {
				monitoringAction = MonitoringAction::SUBTRACT_RIGHT_CHANNEL;
			}
			else if (audioRecorder.recorder->inputHasNoRightChannel()) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else if (micPluggedIn) { // External mic
			if (audioRecorder.recorder->inputHasNoRightChannel()) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else { // Internal mic
			monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
		}
	}
}
void scheduleMidiGateOutISR(uint32_t saddrPosAtStart, int32_t unadjustedNumSamplesBeforeLappingPlayHead,
                            int32_t timeWithinWindowAtWhichMIDIOrGateOccurs) {
	bool anyGateOutputPending = cvEngine.isAnythingPending();

	if ((midiEngine.anythingInOutputBuffer() || anyGateOutputPending) && !isTimerEnabled(TIMER_MIDI_GATE_OUTPUT)) {

		// I don't think this actually could still get left at -1, but just in case...
		if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
			timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
		}

		uint32_t saddrAtEnd = (uint32_t)(getTxBufferCurrentPlace());
		uint32_t saddrPosAtEnd = saddrAtEnd >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE);
		uint32_t saddrMovementSinceStart =
		    saddrPosAtEnd - saddrPosAtStart; // You'll need to &(SSI_TX_BUFFER_NUM_SAMPLES - 1) this anytime it's used

		int32_t samplesTilMIDIOrGate = (timeWithinWindowAtWhichMIDIOrGateOccurs - saddrMovementSinceStart
		                                - unadjustedNumSamplesBeforeLappingPlayHead)
		                               & (SSI_TX_BUFFER_NUM_SAMPLES - 1);

		if (!samplesTilMIDIOrGate) {
			samplesTilMIDIOrGate = SSI_TX_BUFFER_NUM_SAMPLES;
		}

		// samplesTilMIDI += 10; This gets the start of stuff perfectly lined up. About 10 for MIDI, 12 for gate

		if (anyGateOutputPending) {
			// If a gate note-on was processed at the same time as a gate note-off, the note-off will have already
			// been sent, but we need to make sure that the note-on now doesn't happen until a set amount of time
			// after the note-off.
			int32_t gateMinDelayInSamples =
			    ((uint32_t)cvEngine.minGateOffTime * 289014) >> 16; // That's *4.41 (derived from 44100)
			int32_t samplesTilAllowedToSend =
			    cvEngine.mostRecentSwitchOffTimeOfPendingNoteOn + gateMinDelayInSamples - audioSampleTimer;

			if (samplesTilAllowedToSend > 0) {

				samplesTilAllowedToSend -= (saddrMovementSinceStart & (SSI_TX_BUFFER_NUM_SAMPLES - 1));

				if (samplesTilMIDIOrGate < samplesTilAllowedToSend) {
					samplesTilMIDIOrGate = samplesTilAllowedToSend;
				}
			}
		}

		R_INTC_Enable(INTC_ID_TGIA[TIMER_MIDI_GATE_OUTPUT]);

		// Set delay time. This is samplesTilMIDIOrGate * 515616 / kSampleRate.
		*TGRA[TIMER_MIDI_GATE_OUTPUT] = ((uint32_t)samplesTilMIDIOrGate * 766245) >> 16;
		enableTimer(TIMER_MIDI_GATE_OUTPUT);
	}
}

void routine() {

	logAction("AudioDriver::routine");

	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		ignoreForStats();
		return; // Prevents this from being called again from inside any e.g. memory allocation routines that get
		        // called from within this!
	}

	audioRoutineLocked = true;

	numRoutines = 0;
	if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
		while (doSomeOutputting() && numRoutines < 2) {

#ifndef USE_TASK_MANAGER
			if (numRoutines > 0) {
				deluge::hid::encoders::readEncoders();
				deluge::hid::encoders::interpretEncoders(true);
			}
#endif
			routine_();
			routineBeenCalled = true;
			numRoutines += 1;
		}
	}
	else {
		if (!sdRoutineLock) {
			auto timeNow = getSystemTime();
			while (getSystemTime() < timeNow + 32 / 44100.) {
				size_t numSamples = 32;
				int32_t timeWithinWindowAtWhichMIDIOrGateOccurs;
				tickSongFinalizeWindows(numSamples, timeWithinWindowAtWhichMIDIOrGateOccurs);

				numSamplesLastTime = numSamples;
				renderAudioForStemExport(numSamples);
				audioSampleTimer += numSamples;
				doSomeOutputting();
				// gross and hacky way to make sure the audio recorder writes all of its data so it can't be stolen
				while (audioRecorder.recorder->firstUnwrittenClusterIndex
				       < audioRecorder.recorder->currentRecordClusterIndex) {
					doRecorderCardRoutines();
				}

				audioFileManager.loadAnyEnqueuedClusters(128, false);
			}
		}
	}
	audioRoutineLocked = false;
}

int32_t getNumSamplesLeftToOutputFromPreviousRender() {
	return ((uint32_t)renderingBufferOutputEnd - (uint32_t)renderingBufferOutputPos) >> 3;
}

// Returns whether we got to the end
bool doSomeOutputting() {

	// Copy to actual output buffer, and apply heaps of gain too, with clipping
	int32_t numSamplesOutputted = 0;

	StereoSample* __restrict__ outputBufferForResampling = (StereoSample*)spareRenderingBuffer;
	StereoSample* __restrict__ renderingBufferOutputPosNow = renderingBufferOutputPos;
	int32_t* __restrict__ i2sTXBufferPosNow = (int32_t*)i2sTXBufferPos;
	int32_t* __restrict__ inputReadPos = (int32_t*)i2sRXBufferPos;

	while (renderingBufferOutputPosNow != renderingBufferOutputEnd) {

		// If we've reached the end of the known space in the output buffer...
		if (!(((uint32_t)((uint32_t)i2sTXBufferPosNow - saddr) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
		      & (SSI_TX_BUFFER_NUM_SAMPLES - 1))) {

			// See if there's now some more space.
			saddr = (uint32_t)getTxBufferCurrentPlace();

			// If there wasn't, stop for now
			if (!(((uint32_t)((uint32_t)i2sTXBufferPosNow - saddr) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
			      & (SSI_TX_BUFFER_NUM_SAMPLES - 1))) {
				break;
			}
		}

		// Here we're going to do something equivalent to a multiply_32x32_rshift32(), but add dithering.
		// Because dithering is good, and also because the annoying codec chip freaks out if it sees the same 0s for
		// too long.
		int64_t lAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->l * (int64_t)masterVolumeAdjustmentL + (int64_t)getNoise();
		int64_t rAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->r * (int64_t)masterVolumeAdjustmentR + (int64_t)getNoise();

		int32_t lAdjusted = lAdjustedBig >> 32;
		int32_t rAdjusted = rAdjustedBig >> 32;

		if (doMonitoring) {

			if (monitoringAction == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
				int32_t value = (inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS + 1))
				                - (inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS));
				lAdjusted += value;
				rAdjusted += value;
			}

			else {
				lAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);

				if (monitoringAction == MonitoringAction::NONE) {
					rAdjusted += inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
				}
				else { // Remove right channel
					rAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
				}
			}

			inputReadPos += NUM_MONO_INPUT_CHANNELS;
			if (inputReadPos >= getRxBufferEnd()) {
				inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}

#if HARDWARE_TEST_MODE
		// Send a square wave if anything pressed
		if (anythingProbablyPressed) {
			int32_t outputSample = 1 << 29;
			if ((audioSampleTimer >> 6) & 1) {
				outputSample = -outputSample;
			}

			i2sTXBufferPos->l = outputSample;
			i2sTXBufferPos->r = outputSample;
		}

		// Otherwise, echo input
		else {
			i2sTXBufferPos->l = i2sRXBufferPos->l;
			i2sTXBufferPos->r = i2sRXBufferPos->r;
		}

#else
		outputBufferForResampling[numSamplesOutputted].l = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
		outputBufferForResampling[numSamplesOutputted].r = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);
		if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
			i2sTXBufferPosNow[0] = outputBufferForResampling[numSamplesOutputted].l;
			i2sTXBufferPosNow[1] = outputBufferForResampling[numSamplesOutputted].r;
		}
		// otherwise output 0 or 1 for silence, the DAC doesn't like all zeroes
		else {
			i2sTXBufferPosNow[0] = numSamplesOutputted % 2;
			i2sTXBufferPosNow[1] = numSamplesOutputted % 2;
		}
#endif

#if ALLOW_SPAM_MODE
		if (inSpamMode) {
			i2sTXBufferPosNow[0] = getNoise() >> 4;
			i2sTXBufferPosNow[1] = getNoise() >> 4;
		}
#endif

		i2sTXBufferPosNow += NUM_MONO_OUTPUT_CHANNELS;
		if (i2sTXBufferPosNow == getTxBufferEnd()) {
			i2sTXBufferPosNow = getTxBufferStart();
		}

		numSamplesOutputted++;
		++renderingBufferOutputPosNow;
	}

	renderingBufferOutputPos = renderingBufferOutputPosNow; // Write back from __restrict__ pointer to permanent pointer
	i2sTXBufferPos = (uint32_t)i2sTXBufferPosNow;

	if (numSamplesOutputted) {

		i2sRXBufferPos += (numSamplesOutputted << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd()) {
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
		}

		// Go through each SampleRecorder, feeding them audio
		for (SampleRecorder* recorder = firstRecorder; recorder; recorder = recorder->next) {

			if (recorder->status >= RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
				continue;
			}

			// Recording final output
			if (recorder->mode == AudioInputChannel::OUTPUT) {
				recorder->feedAudio((int32_t*)outputBufferForResampling, numSamplesOutputted);
			}

			// Recording from an input source
			else if (recorder->mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

				// We'll feed a bunch of samples to the SampleRecorder - normally all of what we just advanced, but
				// if the buffer wrapped around, we'll just go to the end of the buffer, and not worry about the
				// extra bit at the start - that'll get done next time.
				uint32_t stopPos =
				    (i2sRXBufferPos < (uint32_t)recorder->sourcePos) ? (uint32_t)getRxBufferEnd() : i2sRXBufferPos;

				int32_t* streamToRecord = recorder->sourcePos;
				int32_t numSamplesFeedingNow =
				    (stopPos - (uint32_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);

				// We also enforce a firm limit on how much to feed, to keep things sane. Any remaining will get
				// done next time.
				numSamplesFeedingNow = std::min(numSamplesFeedingNow, 256_i32);

				if (recorder->mode == AudioInputChannel::RIGHT) {
					streamToRecord++;
				}

				recorder->feedAudio(streamToRecord, numSamplesFeedingNow);

				recorder->sourcePos += numSamplesFeedingNow << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				if (recorder->sourcePos >= getRxBufferEnd()) {
					recorder->sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				}
			}
		}
	}

	return (renderingBufferOutputPos == renderingBufferOutputEnd);
}

void logAction(char const* string) {
#if DO_AUDIO_LOG
	if (numAudioLogItems >= AUDIO_LOG_SIZE)
		return;
	audioLogTimes[numAudioLogItems] = *TCNT[TIMER_SYSTEM_FAST];
	strcpy(audioLogStrings[numAudioLogItems], string);
	numAudioLogItems++;
#endif
}

void logAction(int32_t number) {
#if DO_AUDIO_LOG
	char buffer[12];
	intToString(number, buffer);
	logAction(buffer);
#endif
}

void dumpAudioLog() {
#if DO_AUDIO_LOG
	uint16_t currentTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t timePassedA = (uint16_t)currentTime - lastRoutineTime;
	uint32_t timePassedUSA = fastTimerCountToUS(timePassedA);
	if (definitelyLog || timePassedUSA > (storageManager.devVarA * 10)) {

		D_PRINTLN("");
		for (int32_t i = 0; i < numAudioLogItems; i++) {
			uint16_t timePassed = (uint16_t)audioLogTimes[i] - lastRoutineTime;
			uint32_t timePassedUS = fastTimerCountToUS(timePassed);
			D_PRINTLN("%d:  %s", timePassedUS, audioLogStrings[i]);
		}

		D_PRINTLN("%d: end", timePassedUSA);
	}
	definitelyLog = false;
	lastRoutineTime = *TCNT[TIMER_SYSTEM_FAST];
	numAudioLogItems = 0;
#endif
}

void updateReverbParams() {

	// If reverb sidechain on "auto" settings...
	if (reverbSidechainVolume < 0) {

		// Just leave everything as is if parts deleted cos loading new song
		if (getCurrentUI() == &loadSongUI && loadSongUI.deletedPartsOfOldSong) {
			return;
		}

		Sound* soundWithMostReverb = NULL;
		ParamManager* paramManagerWithMostReverb = NULL;
		GlobalEffectableForClip* globalEffectableWithMostReverb = NULL;

		// Set the initial "highest amount found" to that of the song itself, which can't be affected by sidechain.
		// If nothing found with more reverb, then we don't want the reverb affected by sidechain
		int32_t highestReverbAmountFound =
		    currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);

		for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
			thisOutput->getThingWithMostReverb(&soundWithMostReverb, &paramManagerWithMostReverb,
			                                   &globalEffectableWithMostReverb, &highestReverbAmountFound);
		}

		ModControllableAudio* modControllable;

		if (soundWithMostReverb) {
			modControllable = soundWithMostReverb;

			ParamDescriptor paramDescriptor;
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_REVERB_SEND);

			PatchCableSet* patchCableSet = paramManagerWithMostReverb->getPatchCableSet();

			int32_t whichCable = patchCableSet->getPatchCableIndex(PatchSource::SIDECHAIN, paramDescriptor);
			if (whichCable != 255) {
				reverbSidechainVolumeInEffect =
				    patchCableSet->getModifiedPatchCableAmount(whichCable, params::GLOBAL_VOLUME_POST_REVERB_SEND);
			}
			else {
				reverbSidechainVolumeInEffect = 0;
			}
			goto sidechainFound;
		}
		else if (globalEffectableWithMostReverb) {
			modControllable = globalEffectableWithMostReverb;

			reverbSidechainVolumeInEffect =
			    globalEffectableWithMostReverb->getSidechainVolumeAmountAsPatchCableDepth(paramManagerWithMostReverb);

sidechainFound:
			reverbSidechainShapeInEffect =
			    paramManagerWithMostReverb->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_SHAPE);
			reverbSidechain.attack = modControllable->sidechain.attack;
			reverbSidechain.release = modControllable->sidechain.release;
			reverbSidechain.syncLevel = modControllable->sidechain.syncLevel;
			return;
		}

		// Or if no thing has more reverb than the Song itself...
		else {
			reverbSidechainVolumeInEffect = 0;
			return;
		}
	}

	// Otherwise, use manually set params for reverb sidechain
	reverbSidechainVolumeInEffect = reverbSidechainVolume;
	reverbSidechainShapeInEffect = reverbSidechainShape;
}

void registerSideChainHit(int32_t strength) {
	sideChainHitPending = combineHitStrengths(strength, sideChainHitPending);
}

void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound) {
	stopAnyPreviewing();
	MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
	if (!range) {
		return;
	}
	range->sampleHolder.filePath.set(path);
	Error error = range->sampleHolder.loadFile(false, true, true, CLUSTER_LOAD_IMMEDIATELY, filePointer);

	if (error != Error::NONE) {
		display->displayError(error); // Rare, shouldn't cause later problems.
	}

	if (shouldActuallySound) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);
		sampleForPreview->Sound::noteOn(modelStack, &sampleForPreview->arpeggiator, kNoteForDrum, zeroMPEValues);
		bypassCulling = true; // Needed - Dec 2021. I think it's during SampleBrowser::selectEncoderAction() that we
		                      // may have gone a while without an audio routine call.
	}
}

void stopAnyPreviewing() {
	sampleForPreview->unassignAllVoices();
	if (sampleForPreview->sources[0].ranges.getNumElements()) {
		MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(NULL);
	}
}

void getReverbParamsFromSong(Song* song) {
	reverb.setModel(song->model);
	reverb.setRoomSize(song->reverbRoomSize);
	reverb.setDamping(song->reverbDamp);
	reverb.setWidth(song->reverbWidth);
	reverbPan = song->reverbPan;
	reverbSidechainVolume = song->reverbSidechainVolume;
	reverbSidechainShape = song->reverbSidechainShape;
	reverbSidechain.attack = song->reverbSidechainAttack;
	reverbSidechain.release = song->reverbSidechainRelease;
	reverbSidechain.syncLevel = song->reverbSidechainSync;
}

Voice* solicitVoice(Sound* forSound) {
	Voice* newVoice;

	if (firstUnassignedVoice) {
		newVoice = firstUnassignedVoice;
		// reconstruct it
		new (newVoice) Voice();
		firstUnassignedVoice = firstUnassignedVoice->nextUnassigned;
	}

	else {

		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Voice));
		if (!memory) {
			if (activeVoices.getNumElements()) {
				memory = cullVoice(true, HARD, numSamplesLastTime, forSound);
			}
			else {
				return NULL;
			}
		}

		newVoice = new (memory) Voice();
	}

	newVoice->assignedToSound = forSound;

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)forSound;
	keyWords[1] = (uint32_t)newVoice;

	int32_t i = activeVoices.insertAtKeyMultiWord(keyWords);
	if (i == -1) {
		// if (ALPHA_OR_BETA_VERSION) FREEZE_WITH_ERROR("E193"); // No, having run out of RAM here isn't a reason to
		// not continue.
		disposeOfVoice(newVoice);
		return NULL;
	}

	if (forSound->numVoicesAssigned >= forSound->maxVoiceCount) {
		cullVoice(false, SOFT_ALWAYS, numSamplesLastTime, forSound);
	}
	return newVoice;
}

// **** This is the main function that enacts the unassigning of the Voice
// sound only required if removing from vector
// modelStack can be NULL if you really insist
void unassignVoice(Voice* voice, Sound* sound, ModelStackWithSoundFlags* modelStack, bool removeFromVector,
                   bool shouldDispose) {

	activeVoices.checkVoiceExists(voice, sound, "E195");

	voice->setAsUnassigned(modelStack ? modelStack->addVoice(voice) : nullptr);
	if (removeFromVector) {
		uint32_t keyWords[2];
		keyWords[0] = (uint32_t)sound;
		keyWords[1] = (uint32_t)voice;
		activeVoices.deleteAtKeyMultiWord(keyWords);
	}

	if (shouldDispose) {
		if (voice >= staticVoices.begin() && voice < staticVoices.end()) {
			voice->nextUnassigned = firstUnassignedVoice;
			firstUnassignedVoice = voice;
		}
		else {
			disposeOfVoice(voice);
		}
	}
}

void disposeOfVoice(Voice* voice) {
	delugeDealloc(voice);
}

VoiceSample* solicitVoiceSample() {
	if (firstUnassignedVoiceSample) [[likely]] {
		VoiceSample* toReturn = firstUnassignedVoiceSample;
		firstUnassignedVoiceSample = firstUnassignedVoiceSample->nextUnassigned;
		return toReturn;
	}
	else {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(VoiceSample));
		if (!memory) {
			return NULL;
		}

		return new (memory) VoiceSample();
	}
}

void voiceSampleUnassigned(VoiceSample* voiceSample) {
	if (voiceSample >= voiceSamples.begin() && voiceSample < voiceSamples.end()) {
		voiceSample->nextUnassigned = firstUnassignedVoiceSample;
		firstUnassignedVoiceSample = voiceSample;
	}
	else {
		delugeDealloc(voiceSample);
	}
}

TimeStretcher* solicitTimeStretcher() {
	if (firstUnassignedTimeStretcher) {

		TimeStretcher* toReturn = firstUnassignedTimeStretcher;
		firstUnassignedTimeStretcher = firstUnassignedTimeStretcher->nextUnassigned;
		return toReturn;
	}

	else {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(TimeStretcher));
		if (!memory) {
			return NULL;
		}

		return new (memory) TimeStretcher();
	}
}

// There are no destructors. You gotta clean it up before you call this
void timeStretcherUnassigned(TimeStretcher* timeStretcher) {
	if (timeStretcher >= timeStretchers.begin() && timeStretcher < timeStretchers.end()) {
		timeStretcher->nextUnassigned = firstUnassignedTimeStretcher;
		firstUnassignedTimeStretcher = timeStretcher;
	}
	else {
		delugeDealloc(timeStretcher);
	}
}

// TODO: delete unused ones
LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate) {
	const auto idx = util::to_underlying(inputType) - util::to_underlying(OscType::INPUT_L);
	if (!liveInputBuffers[idx]) {
		if (!mayCreate) {
			return NULL;
		}

		int32_t size = sizeof(LiveInputBuffer);
		if (inputType == OscType::INPUT_STEREO) {
			size += kInputRawBufferSize * sizeof(int32_t);
		}

		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(size);
		if (!memory) {
			return NULL;
		}

		liveInputBuffers[idx] = new (memory) LiveInputBuffer();
	}

	return liveInputBuffers[idx];
}

bool createdNewRecorder;

void doRecorderCardRoutines() {

	SampleRecorder** prevPointer = &firstRecorder;
	int32_t count = 0;
	while (true) {
		count++;

		SampleRecorder* recorder = *prevPointer;
		if (!recorder) {
			break;
		}

		Error error = recorder->cardRoutine();
		if (error != Error::NONE) {
			display->displayError(error);
		}

		// If, while in the card routine, a new Recorder was added, then our linked list traversal state thing will
		// be out of wack, so let's just get out and come back later
		if (createdNewRecorder) {
			break;
		}

		// If complete, discard it
		if (recorder->status == RecorderStatus::AWAITING_DELETION) {
			D_PRINTLN("deleting recorder");
			*prevPointer = recorder->next;
			recorder->~SampleRecorder();
			delugeDealloc(recorder);
		}

		// Otherwise, move on
		else {
			prevPointer = &recorder->next;
		}
	}

	if (ALPHA_OR_BETA_VERSION && ENABLE_CLIP_CUTTING_DIAGNOSTICS && count >= 10 && !display->hasPopup()) {
		display->displayPopup("MORE");
	}
}

void slowRoutine() {
	if (sdRoutineLock) {
		// can happen if the SD routine is yielding
		return;
	}
	// The RX buffer is much bigger than the TX, and we get our timing from the TX buffer's sending.
	// However, if there's an audio glitch, and also at start-up, it's possible we might have missed an entire cycle
	// of the TX buffer. That would cause the RX buffer's latency to increase. So here, we check for that and
	// correct it. The correct latency for the RX buffer is between (SSI_TX_BUFFER_NUM_SAMPLES) and (2 *
	// SSI_TX_BUFFER_NUM_SAMPLES).

	uint32_t rxBufferWriteAddr = (uint32_t)getRxBufferCurrentPlace();
	uint32_t latencyWithinAppropriateWindow =
	    (((rxBufferWriteAddr - (uint32_t)i2sRXBufferPos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE))
	     - SSI_TX_BUFFER_NUM_SAMPLES)
	    & (SSI_RX_BUFFER_NUM_SAMPLES - 1);

	while (latencyWithinAppropriateWindow >= SSI_TX_BUFFER_NUM_SAMPLES) {
		i2sRXBufferPos += (SSI_TX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd()) {
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
		}
		latencyWithinAppropriateWindow =
		    (((rxBufferWriteAddr - (uint32_t)i2sRXBufferPos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE))
		     - SSI_TX_BUFFER_NUM_SAMPLES)
		    & (SSI_RX_BUFFER_NUM_SAMPLES - 1);
	}

	// Discard any LiveInputBuffers which aren't in use
	for (int32_t i = 0; i < 3; i++) {
		if (liveInputBuffers[i]) {
			if (liveInputBuffers[i]->upToTime != audioSampleTimer) {
				liveInputBuffers[i]->~LiveInputBuffer();
				delugeDealloc(liveInputBuffers[i]);
				liveInputBuffers[i] = NULL;
			}
		}
	}

	createdNewRecorder = false;

	// Go through all SampleRecorders, getting them to write etc
	doRecorderCardRoutines();
}

SampleRecorder* getNewRecorder(int32_t numChannels, AudioRecordingFolder folderID, AudioInputChannel mode,
                               bool keepFirstReasons, bool writeLoopPoints, int32_t buttonPressLatency,
                               bool shouldNormalize, Output* outputRecordingFrom) {
	Error error;

	void* recorderMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SampleRecorder));
	if (!recorderMemory) {
		return NULL;
	}

	SampleRecorder* newRecorder = new (recorderMemory) SampleRecorder();

	error = newRecorder->setup(numChannels, mode, keepFirstReasons, writeLoopPoints, folderID, buttonPressLatency,
	                           outputRecordingFrom);
	if (error != Error::NONE) {
errorAfterAllocation:
		newRecorder->~SampleRecorder();
		delugeDealloc(recorderMemory);
		return NULL;
	}

	if (mode == AudioInputChannel::SPECIFIC_OUTPUT) {
		if (!outputRecordingFrom) {
			D_PRINTLN("Specific output recorder with no output provided");
			goto errorAfterAllocation;
		}
		bool success = outputRecordingFrom->addRecorder(newRecorder);
		if (!success) {
			D_PRINTLN("Tried to attach to an occupied output");
			goto errorAfterAllocation;
		}
	}

	newRecorder->next = firstRecorder;
	firstRecorder = newRecorder;

	createdNewRecorder = true;

	if (mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		renderInStereo = true; // Quickly set it to true. It'll keep getting reset to true now every time
		                       // inputRoutine() is called
	}

	return newRecorder;
}

// PLEASE don't call this if there's any chance you might be in the SD card routine...
void discardRecorder(SampleRecorder* recorder) {
	int32_t count = 0;
	SampleRecorder** prevPointer = &firstRecorder;
	while (*prevPointer) {

		count++;
		if (ALPHA_OR_BETA_VERSION && !*prevPointer) {
			FREEZE_WITH_ERROR("E264");
		}
		if (*prevPointer == recorder) {
			*prevPointer = recorder->next;
			break;
		}

		prevPointer = &(*prevPointer)->next;
	}

	recorder->~SampleRecorder();
	delugeDealloc(recorder);
}

bool isAnyInternalRecordingHappening() {
	for (SampleRecorder* recorder = firstRecorder; recorder; recorder = recorder->next) {
		if (recorder->mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
			return true;
		}
	}

	return false;
}

} // namespace AudioEngine

//     for (Voice* thisVoice = voices; thisVoice != &voices[numVoices]; thisVoice++) {
