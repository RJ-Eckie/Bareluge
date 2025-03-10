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

#include "processing/sound/sound.h"
#include "definitions_cxx.hpp"
#include "dsp/dx/engine.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_vector.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "modulation/patch/patcher.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/misc.h"

namespace params = deluge::modulation::params;

extern "C" {
#include "RZA1/mtu/mtu.h"
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

const PatchableInfo patchableInfoForSound = {
    (int32_t)(offsetof(Sound, paramFinalValues) - offsetof(Sound, patcher) - (params::FIRST_GLOBAL * sizeof(int32_t))),
    offsetof(Sound, globalSourceValues) - offsetof(Sound, patcher),
    params::FIRST_GLOBAL,
    params::FIRST_GLOBAL_NON_VOLUME,
    params::FIRST_GLOBAL_HYBRID,
    params::FIRST_GLOBAL_EXP,
    params::kNumParams,
    GLOBALITY_GLOBAL};

Sound::Sound() : patcher(&patchableInfoForSound) {
	unpatchedParamKind_ = params::Kind::UNPATCHED_SOUND;

	for (int32_t s = 0; s < kNumSources; s++) {
		oscRetriggerPhase[s] = 0xFFFFFFFF;
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		modulatorRetriggerPhase[m] = 0;
	}

	for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
		monophonicExpressionValues[i] = 0;
	}

	numVoicesAssigned = 0;

	sideChainSendLevel = 0;
	polyphonic = PolyphonyMode::POLY;
	lastNoteCode = -2147483648;

	modulatorTranspose[0] = 0;
	modulatorCents[0] = 0;
	modulatorTranspose[1] = -12;
	modulatorCents[1] = 0;

	transpose = 0;
	modFXType = ModFXType::NONE;

	oscillatorSync = false;

	numUnison = 1;
	unisonDetune = 8;
	unisonStereoSpread = 0;

	synthMode = SynthMode::SUBTRACTIVE;
	modulator1ToModulator0 = false;

	lpfMode = FilterMode::TRANSISTOR_24DB; // Good for samples, I think

	postReverbVolumeLastTime = -1; // Special state to make it grab the actual value the first time it's rendered

	// LFO
	modKnobs[0][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_FX);
	modKnobs[0][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_PAN);

	modKnobs[1][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
	modKnobs[1][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);

	modKnobs[2][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_ENV_0_ATTACK);
	modKnobs[2][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_ENV_0_RELEASE);

	modKnobs[3][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_DELAY_RATE);
	modKnobs[3][0].paramDescriptor.setToHaveParamOnly(params::GLOBAL_DELAY_FEEDBACK);

	modKnobs[4][0].paramDescriptor.setToHaveParamOnly(params::GLOBAL_REVERB_AMOUNT);

	modKnobs[5][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_LFO_FREQ);

	modKnobs[4][1].paramDescriptor.setToHaveParamAndSource(params::GLOBAL_VOLUME_POST_REVERB_SEND,
	                                                       PatchSource::SIDECHAIN);
	modKnobs[5][0].paramDescriptor.setToHaveParamAndSource(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL);

	modKnobs[6][1].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE);
	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO);

	modKnobs[7][1].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START
	                                                  + params::UNPATCHED_SAMPLE_RATE_REDUCTION);
	modKnobs[7][0].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING);
	voicePriority = VoicePriority::MEDIUM;
	whichExpressionSourcesChangedAtSynthLevel = 0;

	skippingRendering = true;
	startSkippingRenderingAtTime = 0;

	paramLPF.p = PARAM_LPF_OFF;

	doneReadingFromFile();
}

void Sound::initParams(ParamManager* paramManager) {

	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = params::Kind::UNPATCHED_SOUND;

	unpatchedParams->params[params::UNPATCHED_ARP_GATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SEQUENCE_LENGTH].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RHYTHM].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_PORTAMENTO].setCurrentValueBasicForSetup(-2147483648);

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_VOLUME].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_OSC_A_VOLUME].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::GLOBAL_VOLUME_POST_FX, 40));
	patchedParams->params[params::GLOBAL_VOLUME_POST_REVERB_SEND].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_FOLD].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_HPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_HPF_FREQ].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_HPF_MORPH].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_LPF_MORPH].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_PITCH_ADJUST].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::GLOBAL_REVERB_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::GLOBAL_DELAY_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::GLOBAL_ARP_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::GLOBAL_DELAY_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_CARRIER_0_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_CARRIER_1_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_0_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_1_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_OSC_A_PHASE_WIDTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_OSC_B_PHASE_WIDTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_1_ATTACK, 20));
	patchedParams->params[params::LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_1_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_1_SUSTAIN, 25));
	patchedParams->params[params::LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_1_RELEASE, 20));
	patchedParams->params[params::LOCAL_LFO_LOCAL_FREQ].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::GLOBAL_LFO_FREQ].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::GLOBAL_LFO_FREQ, 30));
	patchedParams->params[params::LOCAL_PAN].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_NOISE_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::GLOBAL_MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::GLOBAL_MOD_FX_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[params::LOCAL_OSC_A_PITCH_ADJUST].setCurrentValueBasicForSetup(0);       // Don't change
	patchedParams->params[params::LOCAL_OSC_B_PITCH_ADJUST].setCurrentValueBasicForSetup(0);       // Don't change
	patchedParams->params[params::LOCAL_MODULATOR_0_PITCH_ADJUST].setCurrentValueBasicForSetup(0); // Don't change
	patchedParams->params[params::LOCAL_MODULATOR_1_PITCH_ADJUST].setCurrentValueBasicForSetup(0); // Don't change
}

void Sound::setupAsSample(ParamManagerForTimeline* paramManager) {

	polyphonic = PolyphonyMode::AUTO;
	lpfMode = FilterMode::TRANSISTOR_24DB;

	sources[0].oscType = OscType::SAMPLE;
	sources[1].oscType = OscType::SAMPLE;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 0));
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_0_SUSTAIN, 50));
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 0));

	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);

	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_PITCH_ADJUST);

	paramManager->getPatchCableSet()->numPatchCables = 1;
	paramManager->getPatchCableSet()->patchCables[0].setup(PatchSource::VELOCITY, params::LOCAL_VOLUME,
	                                                       getParamFromUserValue(params::PATCH_CABLE, 50));

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

void Sound::setupAsDefaultSynth(ParamManager* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(0x47AE1457);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(0xA2000000);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(0x10000000);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(0x80000000);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(0xE6666654);
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(0x7FFFFFFF);
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(0x851EB851);
	patchedParams->params[params::LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(0xFFFFFFE9);
	patchedParams->params[params::LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(0xE6666654);
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(0x50000000);

	paramManager->getPatchCableSet()->patchCables[0].setup(PatchSource::NOTE, params::LOCAL_LPF_FREQ, 0x08F5C28C);
	paramManager->getPatchCableSet()->patchCables[1].setup(PatchSource::ENVELOPE_1, params::LOCAL_LPF_FREQ, 0x1C28F5B8);
	paramManager->getPatchCableSet()->patchCables[2].setup(PatchSource::VELOCITY, params::LOCAL_LPF_FREQ, 0x0F5C28F0);
	paramManager->getPatchCableSet()->patchCables[3].setup(PatchSource::VELOCITY, params::LOCAL_VOLUME, 0x3FFFFFE8);

	paramManager->getPatchCableSet()->numPatchCables = 4;

	setupDefaultExpressionPatching(paramManager);

	lpfMode = FilterMode::TRANSISTOR_24DB; // Good for samples, I think

	sources[0].oscType = OscType::SAW;
	sources[1].transpose = -12;

	numUnison = 4;
	unisonDetune = 10;

	transpose = -12;

	doneReadingFromFile();
}

void Sound::possiblySetupDefaultExpressionPatching(ParamManager* paramManager) {

	if (smDeserializer.firmware_version < FirmwareVersion::official({4, 0, 0, "beta"})) {

		if (!paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PatchSource::AFTERTOUCH)
		    && !paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PatchSource::X)
		    && !paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PatchSource::Y)) {

			setupDefaultExpressionPatching(paramManager);
		}
	}
}

void Sound::setupDefaultExpressionPatching(ParamManager* paramManager) {
	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	if (patchCableSet->numPatchCables >= kMaxNumPatchCables) {
		return;
	}
	patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(PatchSource::AFTERTOUCH, params::LOCAL_VOLUME,
	                                                                  getParamFromUserValue(params::PATCH_CABLE, 33));

	if (patchCableSet->numPatchCables >= kMaxNumPatchCables) {
		return;
	}

	if (synthMode == SynthMode::FM) {
		patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(
		    PatchSource::Y, params::LOCAL_MODULATOR_0_VOLUME, getParamFromUserValue(params::PATCH_CABLE, 15));
	}
	else {
		patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(
		    PatchSource::Y, params::LOCAL_LPF_FREQ, getParamFromUserValue(params::PATCH_CABLE, 20));
	}
}

void Sound::setupAsBlankSynth(ParamManager* paramManager, bool is_dx) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(2147483647);
	if (is_dx) {
		sources[0].oscType = OscType::DX7;
		sources[0].ensureDxPatch(); // initializes DX engine if this is the first dx7patch
		// velocity is forwarded to dx7 engine, don't do master volume
		paramManager->getPatchCableSet()->numPatchCables = 0;
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(2147483647); // 30 ish
	}
	else {
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(-2147483648);

		paramManager->getPatchCableSet()->numPatchCables = 1;
		paramManager->getPatchCableSet()->patchCables[0].setup(PatchSource::VELOCITY, params::LOCAL_VOLUME,
		                                                       getParamFromUserValue(params::PATCH_CABLE, 50));
	}

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

ModFXType Sound::getModFXType() {
	return modFXType;
}

// Returns false if not enough ram
bool Sound::setModFXType(ModFXType newType) {
	if (newType == ModFXType::FLANGER || newType == ModFXType::CHORUS || newType == ModFXType::CHORUS_STEREO) {
		if (!modFXBuffer) {
			// TODO: should give an error here if no free ram
			modFXBuffer =
			    (StereoSample*)GeneralMemoryAllocator::get().allocLowSpeed(kModFXBufferSize * sizeof(StereoSample));
			if (!modFXBuffer) {
				return false;
			}
		}
		if (modFXGrainBuffer) {
			delugeDealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}
	else if (newType == ModFXType::GRAIN) {
		if (!modFXGrainBuffer) {
			modFXGrainBuffer = (StereoSample*)GeneralMemoryAllocator::get().allocLowSpeed(kModFXGrainBufferSize
			                                                                              * sizeof(StereoSample));
			if (!modFXGrainBuffer) {
				return false;
			}
		}
		if (modFXBuffer) {
			delugeDealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
	}
	else {
		if (modFXBuffer) {
			delugeDealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
		if (modFXGrainBuffer) {
			delugeDealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}

	modFXType = newType;
	clearModFXMemory();
	return true;
}

void Sound::patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
                                           int32_t newValue) {

	recalculatePatchingToParam(p, (ParamManagerForTimeline*)modelStack->paramManager);

	// If we just enabled an oscillator, we need to calculate voices' phase increments
	if (oldValue == -2147483648 && newValue != -2147483648) {

		// This will make inactive any voiceSources which currently have no volume. Ideally we'd only tell it to do the
		// consideration for the oscillator in question, but oh well

		switch (p) {
		case params::LOCAL_OSC_A_VOLUME:
		case params::LOCAL_OSC_B_VOLUME:
		case params::LOCAL_MODULATOR_0_VOLUME:
		case params::LOCAL_MODULATOR_1_VOLUME:
			recalculateAllVoicePhaseIncrements(modelStack);
		}
	}
}

void Sound::recalculatePatchingToParam(uint8_t p, ParamManagerForTimeline* paramManager) {

	Destination* destination = paramManager->getPatchCableSet()->getDestinationForParam(p);
	if (destination) {
		sourcesChanged |= destination->sources; // Pretend those sources have changed, and the param will update - for
		                                        // each Voice too if local.
	}

	// Otherwise, if nothing patched there...
	else {

		// Whether global...
		if (p >= params::FIRST_GLOBAL) {
			patcher.recalculateFinalValueForParamWithNoCables(p, this, paramManager);
		}

		// Or local (do to each voice)...
		else {
			if (numVoicesAssigned) {
				int32_t ends[2];
				AudioEngine::activeVoices.getRangeForSound(this, ends);
				for (int32_t v = ends[0]; v < ends[1]; v++) {
					Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
					thisVoice->patcher.recalculateFinalValueForParamWithNoCables(p, this, paramManager);
				}
			}
		}
	}
}

#define ENSURE_PARAM_MANAGER_EXISTS                                                                                    \
	if (!paramManager->containsAnyMainParamCollections()) {                                                            \
		Error error = createParamManagerForLoading(paramManager);                                                      \
		if (error != Error::NONE)                                                                                      \
			return error;                                                                                              \
	}                                                                                                                  \
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();                      \
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;                  \
	ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();                          \
	PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the
// defaultParams into it). arpSettings optional - no need if you're loading a new V2.0 song where Instruments are all
// separate from Clips and won't store any arp stuff.
Error Sound::readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
                             int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song) {

	if (!strcmp(tagName, "osc1")) {
		Error error = readSourceFromFile(reader, 0, paramManager, readAutomationUpToPos);
		if (error != Error::NONE) {
			return error;
		}
		reader.exitTag("osc1");
	}

	else if (!strcmp(tagName, "osc2")) {
		Error error = readSourceFromFile(reader, 1, paramManager, readAutomationUpToPos);
		if (error != Error::NONE) {
			return error;
		}
		reader.exitTag("osc2");
	}

	else if (!strcmp(tagName, "mode")) {
		char const* contents = reader.readTagOrAttributeValue();
		if (synthMode != SynthMode::RINGMOD) { // Compatibility with old XML files
			synthMode = stringToSynthMode(contents);
		}
		// Uart::print("synth mode set to: ");
		// Uart::println(synthMode);
		reader.exitTag("mode");
	}

	// Backwards-compatible reading of old-style oscs, from pre-mid-2016 files
	else if (!strcmp(tagName, "oscillatorA")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {

			if (!strcmp(tagName, "type")) {
				sources[0].oscType = stringToOscType(reader.readTagOrAttributeValue());
				reader.exitTag("type");
			}
			else if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_VOLUME,
				                         readAutomationUpToPos);
				reader.exitTag("volume");
			}
			else if (!strcmp(tagName, "phaseWidth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_PHASE_WIDTH,
				                         readAutomationUpToPos);
				reader.exitTag("phaseWidth");
			}
			else if (!strcmp(tagName, "note")) {
				int32_t presetNote = std::clamp<int32_t>(reader.readTagOrAttributeValueInt(), 1, 127);

				sources[0].transpose += presetNote - 60;
				sources[1].transpose += presetNote - 60;
				modulatorTranspose[0] += presetNote - 60;
				modulatorTranspose[1] += presetNote - 60;
				reader.exitTag("note");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("oscillatorA");
	}

	else if (!strcmp(tagName, "oscillatorB")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				sources[1].oscType = stringToOscType(reader.readTagOrAttributeValue());
				reader.exitTag("type");
			}
			else if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_VOLUME,
				                         readAutomationUpToPos);
				reader.exitTag("volume");
			}
			else if (!strcmp(tagName, "phaseWidth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_PHASE_WIDTH,
				                         readAutomationUpToPos);
				// setParamPresetValue(params::LOCAL_OSC_B_PHASE_WIDTH, stringToInt(reader.readTagContents()) <<
				// 1); // Special case - pw values are stored at half size in file
				reader.exitTag("phaseWidth");
			}
			else if (!strcmp(tagName, "transpose")) {
				sources[1].transpose += reader.readTagOrAttributeValueInt();
				reader.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				sources[1].cents = reader.readTagOrAttributeValueInt();
				reader.exitTag("cents");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("oscillatorB");
	}

	else if (!strcmp(tagName, "modulator1")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_0_VOLUME,
				                         readAutomationUpToPos);
				reader.exitTag("volume");
			}
			else if (!strcmp(tagName, "transpose")) {
				modulatorTranspose[0] += reader.readTagOrAttributeValueInt();
				reader.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				modulatorCents[0] = reader.readTagOrAttributeValueInt();
				reader.exitTag("cents");
			}
			else if (!strcmp(tagName, "retrigPhase")) {
				modulatorRetriggerPhase[0] = reader.readTagOrAttributeValueInt();
				reader.exitTag("retrigPhase");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("modulator1");
	}

	else if (!strcmp(tagName, "modulator2")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_1_VOLUME,
				                         readAutomationUpToPos);
				reader.exitTag("volume");
			}
			else if (!strcmp(tagName, "transpose")) {
				modulatorTranspose[1] += reader.readTagOrAttributeValueInt();
				reader.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				modulatorCents[1] = reader.readTagOrAttributeValueInt();
				reader.exitTag("cents");
			}
			else if (!strcmp(tagName, "retrigPhase")) {
				modulatorRetriggerPhase[1] = reader.readTagOrAttributeValueInt();
				reader.exitTag("retrigPhase");
			}
			else if (!strcmp(tagName, "toModulator1")) {
				modulator1ToModulator0 = reader.readTagOrAttributeValueInt();
				if (modulator1ToModulator0 != 0) {
					modulator1ToModulator0 = 1;
				}
				reader.exitTag("toModulator1");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("modulator2");
	}

	else if (!strcmp(tagName, "arpeggiator")) {
		// Set default values in case they are not configured
		arpSettings->syncType = SYNC_TYPE_EVEN;
		arpSettings->syncLevel = SYNC_LEVEL_NONE;

		while (*(tagName = reader.readNextTagOrAttributeName())) {

			if (!strcmp(tagName,
			            "rate")) { // This is here for compatibility only for people (Lou and Ian) who saved songs with
				                   // firmware in September 2016
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_ARP_RATE, readAutomationUpToPos);
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "numOctaves")) {
				if (arpSettings) {
					arpSettings->numOctaves = reader.readTagOrAttributeValueInt();
				}
				reader.exitTag("numOctaves");
			}
			else if (!strcmp(tagName, "syncType")) {
				if (arpSettings) {
					arpSettings->syncType = (SyncType)reader.readTagOrAttributeValueInt();
					;
				}
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				if (arpSettings) {
					arpSettings->syncLevel = (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(
					    reader.readTagOrAttributeValueInt());
				}
				reader.exitTag("syncLevel");
			}
			else if (!strcmp(tagName, "octaveMode")) {
				if (arpSettings) {
					arpSettings->octaveMode = stringToArpOctaveMode(reader.readTagOrAttributeValue());
					arpSettings->updatePresetFromCurrentSettings();
				}
				reader.exitTag("octaveMode");
			}
			else if (!strcmp(tagName, "noteMode")) {
				if (arpSettings) {
					arpSettings->noteMode = stringToArpNoteMode(reader.readTagOrAttributeValue());
					arpSettings->updatePresetFromCurrentSettings();
				}
				reader.exitTag("noteMode");
			}
			else if (!strcmp(tagName, "mpeVelocity")) {
				if (arpSettings) {
					arpSettings->mpeVelocity = stringToArpMpeModSource(reader.readTagOrAttributeValue());
				}
				reader.exitTag("mpeVelocity");
			}
			else if (!strcmp(tagName, "arpMode")) {
				if (arpSettings) {
					arpSettings->mode = stringToArpMode(reader.readTagOrAttributeValue());
					arpSettings->updatePresetFromCurrentSettings();
				}
				reader.exitTag("arpMode");
			}
			else if (!strcmp(tagName, "mode")
			         && smDeserializer.firmware_version < FirmwareVersion::community({1, 2, 0})) {
				// Import the old "mode" into the new splitted params "arpMode", "noteMode", and "octaveMode
				// but only if the new params are not already read and set,
				// that is, if we detect they have a value other than default
				if (arpSettings) {
					OldArpMode oldMode = stringToOldArpMode(reader.readTagOrAttributeValue());
					if (arpSettings->mode == ArpMode::OFF && arpSettings->noteMode == ArpNoteMode::UP
					    && arpSettings->octaveMode == ArpOctaveMode::UP) {
						arpSettings->mode = oldModeToArpMode(oldMode);
						arpSettings->noteMode = oldModeToArpNoteMode(oldMode);
						arpSettings->octaveMode = oldModeToArpOctaveMode(oldMode);
						arpSettings->updatePresetFromCurrentSettings();
					}
				}
				reader.exitTag("mode");
			}
			else if (!strcmp(tagName,
			                 "gate")) { // This is here for compatibility only for people (Lou and Ian) who saved songs
				                        // with firmware in September 2016
				ENSURE_PARAM_MANAGER_EXISTS
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_GATE,
				                           readAutomationUpToPos);
				reader.exitTag("gate");
			}
			else {
				reader.exitTag(tagName);
			}
		}

		reader.exitTag("arpeggiator");
	}

	else if (!strcmp(tagName, "transpose")) {
		transpose = reader.readTagOrAttributeValueInt();
		reader.exitTag("transpose");
	}

	else if (!strcmp(tagName, "noiseVolume")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_NOISE_VOLUME, readAutomationUpToPos);
		reader.exitTag("noiseVolume");
	}

	else if (!strcmp(tagName, "ratchetAmount")) {
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_AMOUNT,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetAmount");
	}

	else if (!strcmp(tagName, "ratchetProbability")) {
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetProbability");
	}

	else if (!strcmp(tagName, "sequenceLength")) {
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SEQUENCE_LENGTH,
		                           readAutomationUpToPos);
		reader.exitTag("sequenceLength");
	}

	else if (!strcmp(tagName, "rhythm")) {
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RHYTHM, readAutomationUpToPos);
		reader.exitTag("rhythm");
	}

	else if (!strcmp(tagName,
	                 "portamento")) { // This is here for compatibility only for people (Lou and Ian) who saved songs
		                              // with firmware in September 2016
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PORTAMENTO, readAutomationUpToPos);
		reader.exitTag("portamento");
	}

	// For backwards compatibility. If off, switch off for all operators
	else if (!strcmp(tagName, "oscillatorReset")) {
		int32_t value = reader.readTagOrAttributeValueInt();
		if (!value) {
			for (int32_t s = 0; s < kNumSources; s++) {
				oscRetriggerPhase[s] = 0xFFFFFFFF;
			}
			for (int32_t m = 0; m < kNumModulators; m++) {
				modulatorRetriggerPhase[m] = 0xFFFFFFFF;
			}
		}
		reader.exitTag("oscillatorReset");
	}

	else if (!strcmp(tagName, "unison")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "num")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				numUnison = std::max((int32_t)0, std::min((int32_t)kMaxNumVoicesUnison, contents));
				reader.exitTag("num");
			}
			else if (!strcmp(tagName, "detune")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				unisonDetune = std::clamp(contents, 0_i32, kMaxUnisonDetune);
				reader.exitTag("detune");
			}
			else if (!strcmp(tagName, "spread")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				unisonStereoSpread = std::clamp(contents, 0_i32, kMaxUnisonStereoSpread);
				reader.exitTag("spread");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("unison");
	}

	else if (!strcmp(tagName, "oscAPitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("oscAPitchAdjust");
	}

	else if (!strcmp(tagName, "oscBPitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("oscBPitchAdjust");
	}

	else if (!strcmp(tagName, "mod1PitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_0_PITCH_ADJUST,
		                         readAutomationUpToPos);
		reader.exitTag("mod1PitchAdjust");
	}

	else if (!strcmp(tagName, "mod2PitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_1_PITCH_ADJUST,
		                         readAutomationUpToPos);
		reader.exitTag("mod2PitchAdjust");
	}

	// Stuff from the early-2016 format, for compatibility
	else if (!strcmp(tagName, "fileName")) {
		ENSURE_PARAM_MANAGER_EXISTS

		MultisampleRange* range = (MultisampleRange*)sources[0].getOrCreateFirstRange();
		if (!range) {
			return Error::INSUFFICIENT_RAM;
		}

		range->getAudioFileHolder()->filePath.set(reader.readTagOrAttributeValue());
		sources[0].oscType = OscType::SAMPLE;
		paramManager->getPatchedParamSet()->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 0));
		paramManager->getPatchedParamSet()->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_DECAY, 20));
		paramManager->getPatchedParamSet()->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_SUSTAIN, 50));
		paramManager->getPatchedParamSet()->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 0));
		paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_A_VOLUME].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_OSC_B_VOLUME, 50));
		paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_OSC_B_VOLUME, 0));

		reader.exitTag("fileName");
	}

	else if (!strcmp(tagName, "cents")) {
		int8_t newCents = reader.readTagOrAttributeValueInt();
		// We don't need to call the setTranspose method here, because this will get called soon anyway, once the sample
		// rate is known
		sources[0].cents = (std::max((int8_t)-50, std::min((int8_t)50, newCents)));
		reader.exitTag("cents");
	}
	else if (!strcmp(tagName, "continuous")) {
		sources[0].repeatMode = static_cast<SampleRepeatMode>(reader.readTagOrAttributeValueInt());
		sources[0].repeatMode = std::min(sources[0].repeatMode, static_cast<SampleRepeatMode>(kNumRepeatModes - 1));
		reader.exitTag("continuous");
	}
	else if (!strcmp(tagName, "reversed")) {
		sources[0].sampleControls.reversed = reader.readTagOrAttributeValueInt();
		reader.exitTag("reversed");
	}
	else if (!strcmp(tagName, "zone")) {

		MultisampleRange* range = (MultisampleRange*)sources[0].getOrCreateFirstRange();
		if (!range) {
			return Error::INSUFFICIENT_RAM;
		}

		range->sampleHolder.startMSec = 0;
		range->sampleHolder.endMSec = 0;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = 0;
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			// Because this is for old, early-2016 format, there'll only be seconds and milliseconds in here, not
			// samples
			if (!strcmp(tagName, "startSeconds")) {
				range->sampleHolder.startMSec += reader.readTagOrAttributeValueInt() * 1000;
				reader.exitTag("startSeconds");
			}
			else if (!strcmp(tagName, "startMilliseconds")) {
				range->sampleHolder.startMSec += reader.readTagOrAttributeValueInt();
				reader.exitTag("startMilliseconds");
			}
			else if (!strcmp(tagName, "endSeconds")) {
				range->sampleHolder.endMSec += reader.readTagOrAttributeValueInt() * 1000;
				reader.exitTag("endSeconds");
			}
			else if (!strcmp(tagName, "endMilliseconds")) {
				range->sampleHolder.endMSec += reader.readTagOrAttributeValueInt();
				reader.exitTag("endMilliseconds");
			}
		}
		reader.exitTag("zone");
	}

	else if (!strcmp(tagName, "ringMod")) {
		int32_t contents = reader.readTagOrAttributeValueInt();
		if (contents == 1) {
			synthMode = SynthMode::RINGMOD;
		}
		reader.exitTag("ringMod");
	}

	else if (!strcmp(tagName, "modKnobs")) {

		int32_t k = 0;
		int32_t w = 0;

		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "modKnob")) {

				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "controlsParam")) {
						p = params::fileStringToParam(params::Kind::UNPATCHED_SOUND, reader.readTagOrAttributeValue(),
						                              true);
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(reader.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(reader.readTagOrAttributeValue());
					}
					reader.exitTag(tagName);
				}

				if (k < kNumModButtons) { // Ensure we're not loading more than actually fit in our array
					if (p != params::GLOBAL_NONE
					    && p != params::PLACEHOLDER_RANGE) { // Discard any unlikely "range" ones from before V3.2.0,
						                                     // for complex reasons
						ModKnob* newKnob = &modKnobs[k][w];

						if (s == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							newKnob->paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}

						ensureKnobReferencesCorrectVolume(newKnob);
					}
				}

				w++;
				if (w == kNumPhysicalModKnobs) {
					w = 0;
					k++;
				}
			}
			reader.exitTag();
		}
		reader.exitTag("modKnobs");
	}

	else if (!strcmp(tagName, "patchCables")) {
		ENSURE_PARAM_MANAGER_EXISTS
		paramManager->getPatchCableSet()->readPatchCablesFromFile(reader, readAutomationUpToPos);
		reader.exitTag("patchCables");
	}

	else if (!strcmp(tagName, "volume")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_VOLUME_POST_FX, readAutomationUpToPos);
		reader.exitTag("volume");
	}

	else if (!strcmp(tagName, "pan")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_PAN, readAutomationUpToPos);
		reader.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXType")) {
		bool result =
		    setModFXType(stringToFXType(reader.readTagOrAttributeValue())); // This might not work if not enough RAM
		if (!result) {
			display->displayError(Error::INSUFFICIENT_RAM);
		}
		reader.exitTag("modFXType");
	}

	else if (!strcmp(tagName, "fx")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {

			if (!strcmp(tagName, "type")) {
				bool result = setModFXType(
				    stringToFXType(reader.readTagOrAttributeValue())); // This might not work if not enough RAM
				if (!result) {
					display->displayError(Error::INSUFFICIENT_RAM);
				}
				reader.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_MOD_FX_RATE,
				                         readAutomationUpToPos);
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				// This is for compatibility with old files. Some reverse calculation needs to be done.
				int32_t finalValue = reader.readTagOrAttributeValueInt();
				int32_t i =
				    (1 - pow(1 - ((float)finalValue / 2147483648), (float)1 / 3)) / 0.74 * 4294967296 - 2147483648;
				ENSURE_PARAM_MANAGER_EXISTS
				paramManager->getUnpatchedParamSet()
				    ->params[params::UNPATCHED_MOD_FX_FEEDBACK]
				    .setCurrentValueBasicForSetup(i);
				reader.exitTag("feedback");
			}
			else if (!strcmp(tagName, "offset")) {
				// This is for compatibility with old files. Some reverse calculation needs to be done.
				int32_t contents = reader.readTagOrAttributeValueInt();
				int32_t value = ((int64_t)contents << 8) - 2147483648;
				ENSURE_PARAM_MANAGER_EXISTS
				paramManager->getUnpatchedParamSet()
				    ->params[params::UNPATCHED_MOD_FX_OFFSET]
				    .setCurrentValueBasicForSetup(value);
				reader.exitTag("offset");
			}
			else if (!strcmp(tagName, "depth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_MOD_FX_DEPTH,
				                         readAutomationUpToPos);
				reader.exitTag("depth");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("fx");
	}

	else if (!strcmp(tagName, "lfo1")) {
		// Set default values in case they are not configured.
		lfoConfig[LFO1_ID].syncLevel = SYNC_LEVEL_NONE;
		lfoConfig[LFO1_ID].syncType = SYNC_TYPE_EVEN;

		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				lfoConfig[LFO1_ID].waveType = stringToLFOType(reader.readTagOrAttributeValue());
				reader.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_LFO_FREQ, readAutomationUpToPos);
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "syncType")) {
				lfoConfig[LFO1_ID].syncType = (SyncType)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				lfoConfig[LFO1_ID].syncLevel =
				    (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(reader.readTagOrAttributeValueInt());
				reader.exitTag("syncLevel");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("lfo1");
		resyncGlobalLFO();
	}

	else if (!strcmp(tagName, "lfo2")) {
		// Set default values in case they are not configured.
		lfoConfig[LFO2_ID].syncLevel = SYNC_LEVEL_NONE;
		lfoConfig[LFO2_ID].syncType = SYNC_TYPE_EVEN;

		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				lfoConfig[LFO2_ID].waveType = stringToLFOType(reader.readTagOrAttributeValue());
				reader.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LFO_LOCAL_FREQ,
				                         readAutomationUpToPos);
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "syncType")) {
				lfoConfig[LFO2_ID].syncType = (SyncType)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				lfoConfig[LFO2_ID].syncLevel =
				    (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(reader.readTagOrAttributeValueInt());
				reader.exitTag("syncLevel");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("lfo2");
		// No resync for LFO2
	}

	else if (!strcmp(tagName, "sideChainSend")) {
		sideChainSendLevel = reader.readTagOrAttributeValueInt();
		reader.exitTag("sideChainSend");
	}

	else if (!strcmp(tagName, "lpf")) {
		bool switchedOn = true; // For backwards compatibility with pre November 2015 files
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "status")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				switchedOn = std::max((int32_t)0, std::min((int32_t)1, contents));
				reader.exitTag("status");
			}
			else if (!strcmp(tagName, "frequency")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_FREQ, readAutomationUpToPos);
				reader.exitTag("frequency");
			}
			else if (!strcmp(tagName, "morph")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_MORPH, readAutomationUpToPos);
				reader.exitTag("morph");
			}
			else if (!strcmp(tagName, "resonance")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_RESONANCE,
				                         readAutomationUpToPos);
				reader.exitTag("resonance");
			}
			else if (!strcmp(tagName, "mode")) { // For old, pre- October 2016 files
				lpfMode = stringToLPFType(reader.readTagOrAttributeValue());
				reader.exitTag("mode");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		if (!switchedOn) {
			ENSURE_PARAM_MANAGER_EXISTS
			paramManager->getPatchedParamSet()->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(
			    getParamFromUserValue(params::LOCAL_LPF_FREQ, 50));
		}

		reader.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		bool switchedOn = true; // For backwards compatibility with pre November 2015 files
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "status")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				switchedOn = std::max((int32_t)0, std::min((int32_t)1, contents));
				reader.exitTag("status");
			}
			else if (!strcmp(tagName, "frequency")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_FREQ, readAutomationUpToPos);
				reader.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_RESONANCE,
				                         readAutomationUpToPos);
				reader.exitTag("resonance");
			}
			else if (!strcmp(tagName, "morph")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_MORPH, readAutomationUpToPos);
				reader.exitTag("morph");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		if (!switchedOn) {
			ENSURE_PARAM_MANAGER_EXISTS
			paramManager->getPatchedParamSet()->params[params::LOCAL_HPF_FREQ].setCurrentValueBasicForSetup(
			    getParamFromUserValue(params::LOCAL_HPF_FREQ, 50));
		}

		reader.exitTag("hpf");
	}

	else if (!strcmp(tagName, "envelope1")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_ATTACK,
				                         readAutomationUpToPos);
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_DECAY,
				                         readAutomationUpToPos);
				reader.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_SUSTAIN,
				                         readAutomationUpToPos);
				reader.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_RELEASE,
				                         readAutomationUpToPos);
				reader.exitTag("release");
			}
			else {
				reader.exitTag(tagName);
			}
		}

		reader.exitTag("envelope1");
	}

	else if (!strcmp(tagName, "envelope2")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_ATTACK,
				                         readAutomationUpToPos);
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_DECAY,
				                         readAutomationUpToPos);
				reader.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_SUSTAIN,
				                         readAutomationUpToPos);
				reader.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_RELEASE,
				                         readAutomationUpToPos);
				reader.exitTag("release");
			}
			else {
				reader.exitTag(tagName);
			}
		}

		reader.exitTag("envelope2");
	}

	else if (!strcmp(tagName, "polyphonic")) {
		polyphonic = stringToPolyphonyMode(reader.readTagOrAttributeValue());
		reader.exitTag("polyphonic");
	}

	else if (!strcmp(tagName, "maxVoices")) {
		maxVoiceCount = reader.readTagOrAttributeValueInt();
		reader.exitTag("maxVoices");
	}

	else if (!strcmp(tagName, "voicePriority")) {
		voicePriority = static_cast<VoicePriority>(reader.readTagOrAttributeValueInt());
		reader.exitTag("voicePriority");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_REVERB_AMOUNT, readAutomationUpToPos);
		reader.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "defaultParams")) {
		ENSURE_PARAM_MANAGER_EXISTS
		Sound::readParamsFromFile(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}
	else if (!strcmp(tagName, "waveFold")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_FOLD, readAutomationUpToPos);
		reader.exitTag("waveFold");
	}

	else {
		Error result =
		    ModControllableAudio::readTagFromFile(reader, tagName, paramManager, readAutomationUpToPos, song);
		if (result == Error::NONE) {}
		else if (result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		else if (readTagFromFile(reader, tagName)) {}
		else {
			result = smDeserializer.tryReadingFirmwareTagFromFile(tagName);
			if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
				return result;
			}
			reader.exitTag();
		}
	}

	return Error::NONE;
}

// Exists for the purpose of potentially correcting an incorrect file as it's loaded
void Sound::ensureKnobReferencesCorrectVolume(Knob* knob) {
	int32_t p = knob->paramDescriptor.getJustTheParam();

	if (p == params::GLOBAL_VOLUME_POST_REVERB_SEND || p == params::GLOBAL_VOLUME_POST_FX
	    || p == params::LOCAL_VOLUME) {
		if (knob->paramDescriptor.isJustAParam()) {
			knob->paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_FX);
		}
		else if (knob->paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN) {
			knob->paramDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);
		}
		else {
			knob->paramDescriptor.changeParam(params::LOCAL_VOLUME);
		}
	}
}

// p=255 means we're just querying the source to see if it can be patched to anything
PatchCableAcceptance Sound::maySourcePatchToParam(PatchSource s, uint8_t p, ParamManager* paramManager) {

	if (s == PatchSource::NOTE && isDrum()) {
		return PatchCableAcceptance::DISALLOWED;
	}

	if (p != 255 && s != PatchSource::NOT_AVAILABLE && s >= kFirstLocalSource && p >= params::FIRST_GLOBAL) {
		return PatchCableAcceptance::DISALLOWED; // Can't patch local source to global param
	}

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	switch (p) {
	case params::GLOBAL_NONE:
		return PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_VOLUME:
		return (s != PatchSource::ENVELOPE_0
		        // No envelopes allowed to be patched to volume - this is hardcoded elsewhere
		        && s != PatchSource::ENVELOPE_1
		        // Don't let the sidechain patch to local volume - it's supposed to go to global volume
		        && s != PatchSource::SIDECHAIN)
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_OSC_A_PHASE_WIDTH:
		if (getSynthMode() == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		// if (getSynthMode() == SynthMode::FM || (sources[0].oscType != OscType::SQUARE && sources[0].oscType !=
		// OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
		break;
	case params::LOCAL_OSC_A_VOLUME:
		if (getSynthMode() == SynthMode::RINGMOD) {
			return PatchCableAcceptance::DISALLOWED;
		}
	case params::LOCAL_OSC_A_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(0, paramManager)) ? PatchCableAcceptance::ALLOWED
		                                                                      : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_CARRIER_0_FEEDBACK:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (isSourceActiveEver(0, paramManager)
		        && patchedParams->params[params::LOCAL_CARRIER_0_FEEDBACK].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_OSC_B_PHASE_WIDTH:
		if (getSynthMode() == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		// if (getSynthMode() == SynthMode::FM || (sources[1].oscType != OscType::SQUARE && sources[1].oscType !=
		// OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
		break;
	case params::LOCAL_OSC_B_VOLUME:
		if (getSynthMode() == SynthMode::RINGMOD) {
			return PatchCableAcceptance::DISALLOWED;
		}
	case params::LOCAL_OSC_B_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(1, paramManager)) ? PatchCableAcceptance::ALLOWED
		                                                                      : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_CARRIER_1_FEEDBACK:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (isSourceActiveEver(1, paramManager)
		        && patchedParams->params[params::LOCAL_CARRIER_1_FEEDBACK].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_NOISE_VOLUME:
		if (synthMode == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

		// case params::LOCAL_RANGE:
		// return (rangeAdjustedParam != params::GLOBAL_NONE); // Ideally we'd also check whether the range-adjusted
		// param actually has something patched to it

	case params::LOCAL_LPF_FREQ:
	case params::LOCAL_LPF_MORPH:
	case params::LOCAL_LPF_RESONANCE:
		if (lpfMode == FilterMode::OFF) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

	case params::LOCAL_HPF_FREQ:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_HPF_RESONANCE:
		if (hpfMode == FilterMode::OFF) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_0_FEEDBACK:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].containsSomething(-2147483648)
		        && patchedParams->params[params::LOCAL_MODULATOR_0_FEEDBACK].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_1_VOLUME:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_1_FEEDBACK:
		if (synthMode != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].containsSomething(-2147483648)
		        && patchedParams->params[params::LOCAL_MODULATOR_1_FEEDBACK].containsSomething(-2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::GLOBAL_LFO_FREQ:
		return (lfoConfig[LFO1_ID].syncLevel == SYNC_LEVEL_NONE) ? PatchCableAcceptance::ALLOWED
		                                                         : PatchCableAcceptance::DISALLOWED;

		// Nothing may patch to post-fx volume. This is for manual control only. The sidechain patches to post-reverb
		// volume, and everything else patches to per-voice, "local" volume
	case params::GLOBAL_VOLUME_POST_FX:
		return PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_PITCH_ADJUST:
		if (s == PatchSource::X) {
			return PatchCableAcceptance::DISALLOWED; // No patching X to pitch. This happens automatically.
		}
		break;

	case params::GLOBAL_VOLUME_POST_REVERB_SEND: // Only the sidechain can patch to here
		if (s != PatchSource::SIDECHAIN) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

		// In a perfect world, we'd only allow patching to LFO rates if the LFO as a source is itself patched somewhere
		// usable
	}

	return PatchCableAcceptance::ALLOWED;
}

void Sound::noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCodePreArp,
                   int16_t const* mpeValues, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
                   int32_t velocity, int32_t fromMIDIChannel) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	if (!((synthMode == SynthMode::RINGMOD) || (modelStackWithSoundFlags->checkSourceEverActive(0))
	      || (modelStackWithSoundFlags->checkSourceEverActive(1))
	      || (paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648))))
	    [[unlikely]] {
		return;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings();
	getArpBackInTimeAfterSkippingRendering(arpSettings); // Have to do this before telling the arp to noteOn()

	ArpReturnInstruction instruction;
	instruction.sampleSyncLengthOn = sampleSyncLength;

	// We used to not have to worry about the arpeggiator if one-shot samples etc. But now that we support MPE, we do
	// need to keep track of all sounding notes, even one-shot ones, and the "arpeggiator" is where this is stored.
	// These will get left here even after the note has long gone (for sequenced notes anyway), but I can't actually
	// find any negative consequence of this, or need to ever remove them en masse.
	arpeggiator->noteOn(arpSettings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);

	if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) [[likely]] {
		noteOnPostArpeggiator(modelStackWithSoundFlags, noteCodePreArp, instruction.noteCodeOnPostArp, velocity,
		                      mpeValues, instruction.sampleSyncLengthOn, ticksLate, samplesLate, fromMIDIChannel);
	}
}

void Sound::noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp, int32_t noteCodePostArp,
                                  int32_t velocity, int16_t const* mpeValues, uint32_t sampleSyncLength,
                                  int32_t ticksLate, uint32_t samplesLate, int32_t fromMIDIChannel) {

	Voice* voiceToReuse = NULL;

	Voice* voiceForLegato = NULL;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	// If not polyphonic, stop any notes which are releasing, now
	if (numVoicesAssigned && polyphonic != PolyphonyMode::POLY) [[unlikely]] {

		int32_t ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int32_t v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			// If we're proper-MONO, or it's releasing OR has no sustain / note tails
			if (polyphonic == PolyphonyMode::MONO || thisVoice->envelopes[0].state >= EnvelopeStage::RELEASE
			    || !allowNoteTails(modelStack,
			                       true)) { // allowNoteTails() is very nearly exactly what we want to be calling here,
				                            // though not named after the thing we're looking for here

				// If non-FM and all active sources are samples, do a fast-release (if not already fast-releasing).
				// Otherwise, just unassign (cut instantly)
				if (synthMode == SynthMode::FM) {
justUnassign:
					// Ideally, we want to save this voice to reuse. But we can only do that for the first such one
					if (!voiceToReuse) {
						voiceToReuse = thisVoice;
						thisVoice->unassignStuff(false);
					}

					// Or if we'd already found one, have to just unassign this new one
					else {
						if (ALPHA_OR_BETA_VERSION) {
							AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E198");
						}
						AudioEngine::unassignVoice(thisVoice, this, modelStack);
						v--;
						ends[1]--;
					}
				}
				else {
					for (int32_t s = 0; s < kNumSources; s++) {
						if (isSourceActiveCurrently(s, paramManager) && sources[s].oscType != OscType::SAMPLE) {
							goto justUnassign;
						}
					}

					if (thisVoice->envelopes[0].state != EnvelopeStage::FAST_RELEASE) {
						bool stillGoing = thisVoice->doFastRelease();

						if (!stillGoing) {
							goto justUnassign;
						}
					}
				}
			}

			// Otherwise...
			else {
				voiceForLegato = thisVoice;
				break;
			}
		}
	}

	if (polyphonic == PolyphonyMode::LEGATO && voiceForLegato) [[unlikely]] {
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(voiceForLegato);
		voiceForLegato->changeNoteCode(modelStackWithVoice, noteCodePreArp, noteCodePostArp, fromMIDIChannel,
		                               mpeValues);
	}
	else {

		Voice* newVoice;
		int32_t envelopePositions[kNumEnvelopes];

		if (voiceToReuse) [[unlikely]] {
			newVoice = voiceToReuse;

			// The osc phases and stuff will remain

			for (int32_t e = 0; e < kNumEnvelopes; e++) {
				envelopePositions[e] = voiceToReuse->envelopes[e].lastValue;
			}
		}

		else {
			newVoice = AudioEngine::solicitVoice(this);
			if (!newVoice) {
				return; // Should basically never happen
			}
			numVoicesAssigned++;
			reassessRenderSkippingStatus(
			    modelStack); // Since we potentially just changed numVoicesAssigned from 0 to 1.

			newVoice->randomizeOscPhases(this);
		}

		if (sideChainSendLevel != 0) [[unlikely]] {
			AudioEngine::registerSideChainHit(sideChainSendLevel);
		}

		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(newVoice);

		bool success =
		    newVoice->noteOn(modelStackWithVoice, noteCodePreArp, noteCodePostArp, velocity, sampleSyncLength,
		                     ticksLate, samplesLate, voiceToReuse == NULL, fromMIDIChannel, mpeValues);
		if (success) {
			if (voiceToReuse) {
				for (int32_t e = 0; e < kNumEnvelopes; e++) {
					newVoice->envelopes[e].resumeAttack(envelopePositions[e]);
				}
			}
		}

		else {
			AudioEngine::activeVoices.checkVoiceExists(newVoice, this, "E199");
			AudioEngine::unassignVoice(newVoice, this, modelStack);
		}
	}

	lastNoteCode = noteCodePostArp; // Store for porta. We store that at both note-on and note-off.
}

void Sound::allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator) {

	arpeggiator->reset();

#if ALPHA_OR_BETA_VERSION
	if (!modelStack->paramManager) {
		// Previously we were allowed to receive a NULL paramManager, then would just crudely do an unassignAllVoices().
		// But I'm pretty sure this doesn't exist anymore?
		FREEZE_WITH_ERROR("E403");
	}
#endif

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	noteOffPostArpeggiator(modelStackWithSoundFlags, -32768);
}

// noteCode = -32768 (default) means stop *any* voice, regardless of noteCode
void Sound::noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCode) {
	if (!numVoicesAssigned) {
		return;
	}

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if ((thisVoice->noteCodeAfterArpeggiation == noteCode || noteCode == ALL_NOTES_OFF)
		    && thisVoice->envelopes[0].state < EnvelopeStage::RELEASE) { // Don't bother if it's already "releasing"

			ArpeggiatorSettings* arpSettings = getArpSettings();

			ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);

			// If we have actual arpeggiation, just switch off.
			if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
				goto justSwitchOff;
			}

			// If we're in LEGATO or true-MONO mode and there's another note we can switch back to...
			if ((polyphonic == PolyphonyMode::LEGATO || polyphonic == PolyphonyMode::MONO) && !isDrum()
			    && allowNoteTails(modelStackWithVoice)) { // If no note-tails (i.e. yes one-shot samples etc.), the
				                                          // Arpeggiator will be full of notes which
				// might not be active anymore, cos we were keeping track of them for MPE purposes.
				Arpeggiator* arpeggiator = &((SoundInstrument*)this)->arpeggiator;
				if (arpeggiator->hasAnyInputNotesActive()) {
					ArpNote* arpNote =
					    (ArpNote*)arpeggiator->notes.getElementAddress(arpeggiator->notes.getNumElements() - 1);
					int32_t newNoteCode = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];

					if (polyphonic == PolyphonyMode::LEGATO) {
						thisVoice->changeNoteCode(
						    modelStackWithVoice, newNoteCode, newNoteCode,
						    arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)],
						    arpNote->mpeValues);
						lastNoteCode = newNoteCode;
						// I think we could just return here, too?
					}
					else { // PolyphonyMode::MONO
						noteOnPostArpeggiator(
						    modelStack, newNoteCode, newNoteCode,
						    arpeggiator->lastVelocity, // Interesting - I've made it keep the velocity of presumably the
						                               // note we just switched off. I must have decided that sounded
						                               // best? I think I vaguely remember.
						    arpNote->mpeValues, // ... We take the MPE values from the "keypress" associated with the
						                        // new note we'll sound, though.
						    0, 0, 0, arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]);
						return;
					}
				}
				else {
					goto justSwitchOff;
				}
			}

			else {
justSwitchOff:
				thisVoice->noteOff(modelStackWithVoice);
			}
		}
	}
}

bool Sound::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	// Return yes unless all active sources are play-once samples, or envelope 0 has no sustain

	// If arp on, then definitely yes
	ArpeggiatorSettings* arpSettings = getArpSettings((InstrumentClip*)modelStack->getTimelineCounterAllowNull());
	if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
		return true;
	}

	// If no sustain ever, we definitely can't have tails
	if (!envelopeHasSustainEver(0, (ParamManagerForTimeline*)modelStack->paramManager)) {
		return false;
	}

	// After that if not subtractive (so no samples) or there's some noise, we definitely can have tails
	if (synthMode != SynthMode::SUBTRACTIVE
	    || modelStack->paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	        -2147483648)) {
		return true;
	}

	// If we still don't know, just check there's at least one active oscillator that isn't a one-shot sample without a
	// loop-end point
	bool anyActiveSources = false;
	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = modelStack->checkSourceEverActiveDisregardingMissingSample(s);

		anyActiveSources = sourceEverActive || anyActiveSources;

		if (sourceEverActive
		    && (sources[s].oscType != OscType::SAMPLE || sources[s].repeatMode != SampleRepeatMode::ONCE
		        || (!disregardSampleLoop && sources[s].hasAnyLoopEndPoint()))) {
			return true;
		}
	}

	return !anyActiveSources;
}

int32_t Sound::hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength, int32_t note) {

	if (synthMode == SynthMode::FM) {
		return 0;
	}

	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);

		if (sourceEverActive && sources[s].oscType == OscType::SAMPLE
		    && sources[s].repeatMode == SampleRepeatMode::STRETCH) {
			if (getSampleLength) {
				return sources[s].getLengthInSamplesAtSystemSampleRate(note + transpose, true);
			}
			return 1;
		}
	}

	return 0;
}

// Returns sample length in samples
int32_t Sound::hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int32_t note, bool* anyLooping) {

	if (synthMode == SynthMode::FM) {
		return 0;
	}

	if (isNoiseActiveEver(paramManager)) {
		return 0;
	}

	int32_t maxLength = 0;
	if (anyLooping) {
		*anyLooping = false;
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		if (sources[s].oscType != OscType::SAMPLE) {
			return 0;
		}
		else if (sources[s].repeatMode == SampleRepeatMode::CUT || sources[s].repeatMode == SampleRepeatMode::LOOP) {

			if (anyLooping && sources[s].repeatMode == SampleRepeatMode::LOOP) {
				*anyLooping = true;
			}
			int32_t length = sources[s].getLengthInSamplesAtSystemSampleRate(note);

			// TODO: need a bit here to take into account the fact that the note pitch may well have lengthened or
			// shortened the sample

			maxLength = std::max(maxLength, length);
		}
	}

	return maxLength;
}

bool Sound::hasCutModeSamples(ParamManagerForTimeline* paramManager) {

	if (synthMode == SynthMode::FM) {
		return false;
	}

	if (isNoiseActiveEver(paramManager)) {
		return false;
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		if (sources[s].oscType != OscType::SAMPLE || !sources[s].hasAtLeastOneAudioFileLoaded()
		    || sources[s].repeatMode != SampleRepeatMode::CUT) {
			return false;
		}
	}

	return true;
}

bool Sound::allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager) {

	// If arpeggiator, we can always start very late
	ArpeggiatorSettings* arpSettings = getArpSettings(clip);
	if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
		return true;
	}

	if (synthMode == SynthMode::FM) {
		return false;
	}

	// Basically, if any wave-based oscillators active, or one-shot samples, that means no not allowed
	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		switch (sources[s].oscType) {

		// Sample - generally ok, but not if one-shot
		case OscType::SAMPLE:
			if (sources[s].repeatMode == SampleRepeatMode::ONCE || !sources[s].hasAtLeastOneAudioFileLoaded()) {
				return false; // Not quite sure why the must-be-loaded requirement - maybe something would break if it
				              // tried to do a late start otherwise?
			}
			break;

		// Input - ok
		case OscType::INPUT_L:
		case OscType::INPUT_R:
		case OscType::INPUT_STEREO:
			break;

		// Wave-based - instant fail!
		default:
			return false;
		}
	}

	return true;
}

bool Sound::isSourceActiveCurrently(int32_t s, ParamManagerForTimeline* paramManager) {
	return (synthMode == SynthMode::RINGMOD
	        || getSmoothedPatchedParamValue(params::LOCAL_OSC_A_VOLUME + s, paramManager) != -2147483648)
	       && (synthMode == SynthMode::FM || sources[s].oscType != OscType::SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isSourceActiveEverDisregardingMissingSample(int32_t s, ParamManager* paramManager) {
	return (synthMode == SynthMode::RINGMOD
	        || paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_A_VOLUME + s].containsSomething(-2147483648)
	        || renderingOscillatorSyncEver(paramManager));
}

bool Sound::isSourceActiveEver(int32_t s, ParamManager* paramManager) {
	return isSourceActiveEverDisregardingMissingSample(s, paramManager)
	       && (synthMode == SynthMode::FM || sources[s].oscType != OscType::SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isNoiseActiveEver(ParamManagerForTimeline* paramManager) {
	return (synthMode != SynthMode::FM
	        && paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648));
}

bool Sound::renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager) {
	if (!oscillatorSync) {
		return false;
	}
	if (synthMode == SynthMode::FM) {
		return false;
	}
	return (getSmoothedPatchedParamValue(params::LOCAL_OSC_B_VOLUME, paramManager) != -2147483648
	        || synthMode == SynthMode::RINGMOD);
}

bool Sound::renderingOscillatorSyncEver(ParamManager* paramManager) {
	if (!oscillatorSync) {
		return false;
	}
	if (synthMode == SynthMode::FM) {
		return false;
	}
	return (paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_B_VOLUME].containsSomething(-2147483648)
	        || synthMode == SynthMode::RINGMOD);
}

void Sound::sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack) {
	if (!numVoicesAssigned) {
		return;
	}

	if (sources[s].sampleControls.reversed) {
		markerType = static_cast<MarkerType>(kNumMarkerTypes - 1 - util::to_underlying(markerType));
	}

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);
		bool stillGoing = thisVoice->sampleZoneChanged(modelStackWithVoice, s, markerType);
		if (!stillGoing) {
			AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E200");
			AudioEngine::unassignVoice(thisVoice, this, modelStack);
			v--;
			ends[1]--;
		}
	}
}

// Unlike most functions, this one accepts modelStack as NULL, because when unassigning all voices e.g. on song swap, we
// won't have it.
void Sound::reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX) {

	// TODO: should get the caller to provide this, cos they usually already have it. In fact, should put this on the
	// ModelStack, cos many deeper-nested functions called by this one need it too!
	ArpeggiatorSettings* arpSettings = getArpSettings();

	bool skippingStatusNow =
	    ((numVoicesAssigned == 0) && (delay.repeatsUntilAbandon == 0u) && !stutterer.isStuttering(this)
	     && ((arpSettings == nullptr) || !getArp()->hasAnyInputNotesActive() || arpSettings->mode == ArpMode::OFF));

	if (skippingStatusNow != skippingRendering) {

		if (skippingStatusNow) {

			// We wanna start, skipping, but if MOD fx are on...
			if ((modFXType != ModFXType::NONE) || compressor.getThreshold() > 0) {

				// If we didn't start the wait-time yet, start it now
				if (!startSkippingRenderingAtTime) {

					// But wait, first, maybe we actually have just been instructed to cut the MODFX tail
					if (shouldJustCutModFX) {
doCutModFXTail:
						clearModFXMemory();
						goto yupStartSkipping;
					}

					int32_t waitSamplesModfx = 0;
					switch (modFXType) {
					case ModFXType::CHORUS:
						[[fallthrough]];
					case ModFXType::CHORUS_STEREO:
						waitSamplesModfx = 20 * 44;
						break;
					case ModFXType::GRAIN:
						waitSamplesModfx = 350 * 441;
						break;
					default:
						waitSamplesModfx = (90 * 441);
					}
					int32_t waitSamples = std::max(waitSamplesModfx, compressor.getReleaseMS() * 44);
					startSkippingRenderingAtTime = AudioEngine::audioSampleTimer + waitSamples;
				}

				// Or if already waiting, see if the wait is over yet
				else {
					if ((int32_t)(AudioEngine::audioSampleTimer - startSkippingRenderingAtTime) >= 0) {
						startSkippingRenderingAtTime = 0;
						goto yupStartSkipping;
					}

					// Ok, we wanted to check that before manually cutting the MODFX tail, to save time, but that's
					// still an option...
					if (shouldJustCutModFX) {
						goto doCutModFXTail;
					}
				}
			}
			else {
yupStartSkipping:
				startSkippingRendering(modelStack);
			}
		}
		else {
			stopSkippingRendering(arpSettings);
		}
	}

	else {
		startSkippingRenderingAtTime = 0;
	}
}

void Sound::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                   GlobalEffectableForClip** globalEffectableWithMostReverb,
                                   int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	if (!patchedParams->params[params::GLOBAL_REVERB_AMOUNT].isAutomated()
	    && patchedParams->params[params::GLOBAL_REVERB_AMOUNT].containsSomething(-2147483648)) {

		// We deliberately don't use the LPF'ed param here
		int32_t reverbHere = patchedParams->getValue(params::GLOBAL_REVERB_AMOUNT);
		if (*highestReverbAmountFound < reverbHere) {
			*highestReverbAmountFound = reverbHere;
			*soundWithMostReverb = this;
			*paramManagerWithMostReverb = paramManager;
			*globalEffectableWithMostReverb = NULL;
		}
	}
}

// fromAutomation means whether the changes was caused by automation playing back - as opposed to the user turning the
// knob right now
void Sound::notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
                                    int32_t oldValue, int32_t newValue, bool fromAutomation) {

	if (skippingRendering) {
		goto dontDoLPF;
	}

	if (!shouldDoParamLPF) {
		// If param LPF was active for this param, stop it
		if (paramLPF.p == p) {
			paramLPF.p = PARAM_LPF_OFF;
		}
		goto dontDoLPF;
	}

	// If doing param LPF
	if (params::paramNeedsLPF(p, fromAutomation)) {

		// If the param LPF was already busy...
		if (paramLPF.p != PARAM_LPF_OFF) {
			// If it was a different param, tell it to stop so that we can have it
			if (paramLPF.p != p) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
				ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

				stopParamLPF(modelStackCopy->addSoundFlags());
			}

			// Otherwise, keep its current state, and just tell it it's going somewhere new
			else {
				goto changeWhereParamLPFGoing;
			}
		}

		paramLPF.currentValue = oldValue;

changeWhereParamLPFGoing:
		paramLPF.p = p;
	}

	// Or if not doing param LPF
	else {
dontDoLPF:
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

		patchedParamPresetValueChanged(p, modelStackCopy->addSoundFlags(), oldValue, newValue);
	}
}

void Sound::doParamLPF(int32_t numSamples, ModelStackWithSoundFlags* modelStack) {
	if (paramLPF.p == PARAM_LPF_OFF) {
		return;
	}

	int32_t oldValue = paramLPF.currentValue;

	int32_t diff = (modelStack->paramManager->getPatchedParamSet()->getValue(paramLPF.p) >> 8) - (oldValue >> 8);

	if (diff == 0) {
		stopParamLPF(modelStack);
	}
	else {
		int32_t amountToAdd = diff * numSamples;
		paramLPF.currentValue += amountToAdd;
		// patchedParamPresetValueChanged(paramLPF.p, notifySound, oldValue, paramLPF.currentValue);
		patchedParamPresetValueChanged(paramLPF.p, modelStack, oldValue, paramLPF.currentValue);
	}
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::stopParamLPF(ModelStackWithSoundFlags* modelStack) {
	bool wasActive = paramLPF.p != PARAM_LPF_OFF;
	if (wasActive) {
		int32_t p = paramLPF.p;
		paramLPF.p = PARAM_LPF_OFF; // Must do this first, because the below call will involve the Sound calling us back
		                            // for the current value
		if (modelStack) {
			patchedParamPresetValueChanged(p, modelStack, paramLPF.currentValue,
			                               modelStack->paramManager->getPatchedParamSet()->getValue(p));
		}
	}
}

void Sound::render(ModelStackWithThreeMainThings* modelStack, StereoSample* outputBuffer, int32_t numSamples,
                   int32_t* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust,
                   bool shouldLimitDelayFeedback, int32_t pitchAdjust, SampleRecorder* recorder) {

	if (skippingRendering) {
		compressor.gainReduction = 0;
		return;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	// Do global LFO
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::LFO_GLOBAL)) {
		const auto patchSourceLFOGlobalUnderlying = util::to_underlying(PatchSource::LFO_GLOBAL);

		int32_t old = globalSourceValues[patchSourceLFOGlobalUnderlying];
		// TODO: We don't really need to recompute phase increment unless rate, sync, or
		// playbackHandler.getTimePerInternalTickInverse() has changed. Rate and sync changes
		// already cause a resync. Maybe tempo changes do too? If so, this could be part of
		// the resync logic. Note: same issue exists with LFO2 now that it supports sync.
		globalSourceValues[patchSourceLFOGlobalUnderlying] =
		    globalLFO.render(numSamples, lfoConfig[LFO1_ID], getGlobalLFOPhaseIncrement());
		uint32_t anyChange = (old != globalSourceValues[patchSourceLFOGlobalUnderlying]);
		sourcesChanged |= anyChange << patchSourceLFOGlobalUnderlying;
	}

	for (int s = 0; s < kNumSources; s++) {
		if (sources[s].oscType == OscType::DX7 and sources[s].dxPatch) {
			sources[s].dxPatch->computeLfo(numSamples);
		}
	}

	// Do sidechain
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::SIDECHAIN)) {
		if (sideChainHitPending) {
			sidechain.registerHit(sideChainHitPending);
		}

		const auto patchSourceSidechainUnderlying = util::to_underlying(PatchSource::SIDECHAIN);

		int32_t old = globalSourceValues[patchSourceSidechainUnderlying];
		globalSourceValues[patchSourceSidechainUnderlying] = sidechain.render(
		    numSamples, paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_SHAPE));
		uint32_t anyChange = (old != globalSourceValues[patchSourceSidechainUnderlying]);
		sourcesChanged |= anyChange << patchSourceSidechainUnderlying;
	}

	// Perform the actual patching
	if (sourcesChanged) {
		patcher.performPatching(sourcesChanged, this, paramManager);
	}

	// Setup some reverb-related stuff
	int32_t reverbSendAmount =
	    multiply_32x32_rshift32_rounded(reverbAmountAdjust,
	                                    paramFinalValues[params::GLOBAL_REVERB_AMOUNT - params::FIRST_GLOBAL])
	    << 5;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	// Arpeggiator
	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (arpSettings && arpSettings->mode != ArpMode::OFF) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		uint32_t gateThreshold = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_GATE) + 2147483648;
		uint32_t phaseIncrement =
		    arpSettings->getPhaseIncrement(paramFinalValues[params::GLOBAL_ARP_RATE - params::FIRST_GLOBAL]);
		uint32_t ratchetProbability =
		    (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RATCHET_PROBABILITY) + 2147483648;
		uint32_t ratchetAmount = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RATCHET_AMOUNT) + 2147483648;
		uint32_t sequenceLength =
		    (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_SEQUENCE_LENGTH) + 2147483648;
		uint32_t rhythm = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RHYTHM) + 2147483648;

		ArpReturnInstruction instruction;

		getArp()->render(arpSettings, numSamples, gateThreshold, phaseIncrement, sequenceLength, rhythm, ratchetAmount,
		                 ratchetProbability, &instruction);

		if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
			noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp);
		}

		if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
			noteOnPostArpeggiator(
			    modelStackWithSoundFlags,
			    instruction.arpNoteOn->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)],
			    instruction.noteCodeOnPostArp, instruction.arpNoteOn->velocity, instruction.arpNoteOn->mpeValues,
			    instruction.sampleSyncLengthOn, 0, 0,
			    instruction.arpNoteOn->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]);
		}
	}

	// Setup delay
	Delay::State delayWorkingState{};
	delayWorkingState.delayFeedbackAmount = paramFinalValues[params::GLOBAL_DELAY_FEEDBACK - params::FIRST_GLOBAL];
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = paramFinalValues[params::GLOBAL_DELAY_RATE - params::FIRST_GLOBAL];
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	delay.setupWorkingState(delayWorkingState, timePerTickInverse, numVoicesAssigned != 0);
	delayWorkingState.analog_saturation = 8;

	// Render each voice into a local buffer here
	bool renderingInStereo = renderingVoicesInStereo(modelStackWithSoundFlags);
	static int32_t soundBuffer[SSI_TX_BUFFER_NUM_SAMPLES * 2];
	memset(soundBuffer, 0, (numSamples * sizeof(int32_t)) << renderingInStereo);

	if (numVoicesAssigned) {

		// Very often, we'll just apply panning here at the Sound level rather than the Voice level
		bool applyingPanAtVoiceLevel =
		    (AudioEngine::renderInStereo
		     && paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_PAN));

		// Setup filters
		bool thisHasFilters = hasFilters();
		q31_t lpfMorph = getSmoothedPatchedParamValue(params::LOCAL_LPF_MORPH, paramManager);
		q31_t lpfFreq = getSmoothedPatchedParamValue(params::LOCAL_LPF_FREQ, paramManager);
		q31_t hpfMorph = getSmoothedPatchedParamValue(params::LOCAL_HPF_MORPH, paramManager);
		q31_t hpfFreq = getSmoothedPatchedParamValue(params::LOCAL_HPF_FREQ, paramManager);
		bool doLPF = (thisHasFilters
		              && (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE
		                  || paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_LPF_FREQ)
		                  || (lpfFreq < 2147483602) || (lpfMorph > -2147483648)));
		bool doHPF = (thisHasFilters
		              && (paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_HPF_FREQ)
		                  || (hpfFreq != -2147483648) || (hpfMorph > -2147483648)));
		// Each voice will potentially alter the "sources changed" flags, so store a backup to restore between each
		// voice
		/*
		bool backedUpSourcesChanged[FIRST_UNCHANGEABLE_SOURCE - Local::FIRST_SOURCE];
		bool doneFirstVoice = false;
		*/

		int32_t ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int32_t v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
			/*
			if (!doneFirstVoice) {
			    if (numVoicesAssigned > 1) {
			        memcpy(backedUpSourcesChanged, &sourcesChanged[Local::FIRST_SOURCE], FIRST_UNCHANGEABLE_SOURCE -
			Local::FIRST_SOURCE); doneFirstVoice = true;
			    }
			}
			else memcpy(&sourcesChanged[Local::FIRST_SOURCE], backedUpSourcesChanged, FIRST_UNCHANGEABLE_SOURCE -
			Local::FIRST_SOURCE);
			*/

			ModelStackWithVoice* modelStackWithVoice = modelStackWithSoundFlags->addVoice(thisVoice);

			bool stillGoing = thisVoice->render(modelStackWithVoice, soundBuffer, numSamples, renderingInStereo,
			                                    applyingPanAtVoiceLevel, sourcesChanged, doLPF, doHPF, pitchAdjust);
			if (!stillGoing) {
				AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E201");
				AudioEngine::unassignVoice(thisVoice, this, modelStackWithSoundFlags);
				v--;
				ends[1]--;
			}
		}

		// If just rendered in mono, double that up to stereo now
		if (!renderingInStereo) {
			// We know that nothing's patched to pan, so can read it in this very basic way.
			int32_t pan = paramManager->getPatchedParamSet()->getValue(params::LOCAL_PAN) >> 1;

			int32_t amplitudeL, amplitudeR;
			bool doPanning;
			doPanning = (AudioEngine::renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));
			if (doPanning) {
				for (int32_t i = numSamples - 1; i >= 0; i--) {
					int32_t sampleValue = soundBuffer[i];
					soundBuffer[(i << 1)] = multiply_32x32_rshift32(sampleValue, amplitudeL) << 2;
					soundBuffer[(i << 1) + 1] = multiply_32x32_rshift32(sampleValue, amplitudeR) << 2;
				}
			}
			else {
				for (int32_t i = numSamples - 1; i >= 0; i--) {
					int32_t sampleValue = soundBuffer[i];
					soundBuffer[(i << 1)] = sampleValue;
					soundBuffer[(i << 1) + 1] = sampleValue;
				}
			}
		}

		// Or if rendered in stereo...
		else {
			// And if we're only applying pan here at the Sound level...
			if (!applyingPanAtVoiceLevel) {

				// We know that nothing's patched to pan, so can read it in this very basic way.
				int32_t pan = paramManager->getPatchedParamSet()->getValue(params::LOCAL_PAN) >> 1;

				int32_t amplitudeL, amplitudeR;
				bool doPanning;
				doPanning = (AudioEngine::renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));
				if (doPanning) {
					int32_t* thisSample = soundBuffer;
					int32_t* endSamples = thisSample + (numSamples << 1);
					do {
						*thisSample = multiply_32x32_rshift32(*thisSample, amplitudeL) << 2;
						thisSample++;

						*thisSample = multiply_32x32_rshift32(*thisSample, amplitudeR) << 2;
						thisSample++;
					} while (thisSample != endSamples);
				}
			}
		}
	}
	else {
		if (!delayWorkingState.doDelay) {
			reassessRenderSkippingStatus(modelStackWithSoundFlags);
		}

		if (!renderingInStereo) {
			memset(&soundBuffer[numSamples], 0, numSamples * sizeof(int32_t));
		}
	}

	int32_t postFXVolume = paramFinalValues[params::GLOBAL_VOLUME_POST_FX - params::FIRST_GLOBAL];
	int32_t postReverbVolume = paramFinalValues[params::GLOBAL_VOLUME_POST_REVERB_SEND - params::FIRST_GLOBAL];

	if (postReverbVolumeLastTime == -1) {
		postReverbVolumeLastTime = postReverbVolume;
	}

	int32_t modFXDepth = paramFinalValues[params::GLOBAL_MOD_FX_DEPTH - params::FIRST_GLOBAL];
	int32_t modFXRate = paramFinalValues[params::GLOBAL_MOD_FX_RATE - params::FIRST_GLOBAL];

	processSRRAndBitcrushing((StereoSample*)soundBuffer, numSamples, &postFXVolume, paramManager);
	processFX((StereoSample*)soundBuffer, numSamples, modFXType, modFXRate, modFXDepth, delayWorkingState,
	          &postFXVolume, paramManager);
	processStutter((StereoSample*)soundBuffer, numSamples, paramManager);

	processReverbSendAndVolume((StereoSample*)soundBuffer, numSamples, reverbBuffer, postFXVolume, postReverbVolume,
	                           reverbSendAmount, 0, true);

	q31_t compThreshold = paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_COMPRESSOR_THRESHOLD);
	compressor.setThreshold(compThreshold);
	if (compThreshold > 0) {
		compressor.renderVolNeutral((StereoSample*)soundBuffer, numSamples, postFXVolume);
	}
	else {
		compressor.reset();
	}

	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		// we need to double it because for reasons I don't understand audio clips max volume is half the sample volume
		recorder->feedAudio(soundBuffer, numSamples, true, 2);
	}
	addAudio((StereoSample*)soundBuffer, outputBuffer, numSamples);

	postReverbVolumeLastTime = postReverbVolume;

	sourcesChanged = 0;
	whichExpressionSourcesChangedAtSynthLevel = 0;
	for (int i = 0; i < kNumSources; i++) {
		sources[i].dxPatchChanged = false;
	}

	// Unlike all the other possible reasons we might want to start skipping rendering, delay.repeatsUntilAbandon may
	// have changed state just now.
	if (!delay.repeatsUntilAbandon || startSkippingRenderingAtTime) {
		reassessRenderSkippingStatus(modelStackWithSoundFlags);
	}

	doParamLPF(numSamples, modelStackWithSoundFlags);
}

// This is virtual, and gets extended by drums!
void Sound::setSkippingRendering(bool newSkipping) {
	skippingRendering = newSkipping;
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::startSkippingRendering(ModelStackWithSoundFlags* modelStack) {
	timeStartedSkippingRenderingModFX = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingLFO = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
	// compressor.status = EnvelopeStage::OFF; // Was this doing anything? Have removed, to make all of this completely
	// reversible without doing anything

	setSkippingRendering(true);

	stopParamLPF(modelStack);
}

void Sound::stopSkippingRendering(ArpeggiatorSettings* arpSettings) {
	if (skippingRendering) {

		int32_t modFXTimeOff = AudioEngine::audioSampleTimer
		                       - timeStartedSkippingRenderingModFX; // This variable is a good indicator of whether it
		                                                            // actually was skipping at all

		// If rendering was actually stopped for any length of time...
		if (modFXTimeOff) {

			// Do LFO
			globalLFO.tick(AudioEngine::audioSampleTimer - timeStartedSkippingRenderingLFO,
			               getGlobalLFOPhaseIncrement());

			// Do Mod FX
			modFXLFO.tick(modFXTimeOff, paramFinalValues[params::GLOBAL_MOD_FX_RATE - params::FIRST_GLOBAL]);

			// Do arp
			getArpBackInTimeAfterSkippingRendering(arpSettings);

			// Do sidechain
			// if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::SIDECHAIN)) {
			if (AudioEngine::sizeLastSideChainHit) {
				sidechain.registerHitRetrospectively(AudioEngine::sizeLastSideChainHit,
				                                     AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit);
				//}
			}
			// Special state to make it grab the actual value the first time it's rendered
			postReverbVolumeLastTime = -1;

			// clearModFXMemory(); // No need anymore, now we wait for this to basically empty before starting skipping
		}

		setSkippingRendering(false);
	}
}

void Sound::getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings) {

	if (skippingRendering) {
		if (arpSettings && arpSettings->mode != ArpMode::OFF) {
			uint32_t phaseIncrement =
			    arpSettings->getPhaseIncrement(paramFinalValues[params::GLOBAL_ARP_RATE - params::FIRST_GLOBAL]);
			getArp()->gatePos +=
			    (phaseIncrement >> 8) * (AudioEngine::audioSampleTimer - timeStartedSkippingRenderingArp);

			timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
		}
	}
}

void Sound::unassignAllVoices() {
	if (!numVoicesAssigned) {
		return;
	}

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		// ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
		AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E203");
		AudioEngine::unassignVoice(thisVoice, this, NULL,
		                           false); // Don't remove from Vector - we'll do that below, in bulk
	}

	int32_t numToDelete = ends[1] - ends[0];
	if (numToDelete) {
		AudioEngine::activeVoices.deleteAtIndex(ends[0], numToDelete);
	}

	if (ALPHA_OR_BETA_VERSION) {
		if (numVoicesAssigned > 0) {
			// ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
			FREEZE_WITH_ERROR("E070");
		}
		else if (numVoicesAssigned < 0) {
			// ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
			FREEZE_WITH_ERROR("E071");
		}
	}

	// reassessRenderSkippingStatus(); // Nope, this will get called in voiceUnassigned(), which gets called for each
	// voice we unassign above.
}

void Sound::confirmNumVoices(char const* error) {

	/*
	int32_t voiceCount = 0;
	int32_t reasonCount = 0;
	Voice* endAssignedVoices = audioDriver.endAssignedVoices;
	for (Voice* thisVoice = audioDriver.voices; thisVoice != endAssignedVoices; thisVoice++) {
	    if (thisVoice->assignedToSound == this) {
	        voiceCount++;

	        for (int32_t u = 0; u < maxNumUnison; u++) {
	            for (int32_t s = 0; s < NUM_SOURCES; s++) {
	                for (int32_t l = 0; l < NUM_SAMPLE_CLUSTERS_LOADED_AHEAD; l++) {
	                    if (thisVoice->unisonParts[u].sources[s].clusters[l]) {
	                        reasonCount++;
	                    }
	                }
	            }
	        }

	    }
	}

	if (numVoicesAssigned != voiceCount) {
	    Uart::print("voice count tallied ");
	    Uart::print(numVoicesAssigned);
	    Uart::print(", but actually ");
	    Uart::println(voiceCount);
	    FREEZE_WITH_ERROR(error);
	}

	int32_t reasonCountSources = 0;

	for (int32_t l = 0; l < NUM_SAMPLE_CLUSTERS_LOADED_AHEAD; l++) {
	    if (sources[0].clusters[l]) reasonCountSources++;
	}



	if (sources[0].sample) {
	    int32_t totalNumReasons = sources[0].sample->getTotalNumReasons(reasonCount + reasonCountSources);
	    if (totalNumReasons != reasonCount + reasonCountSources) {

	        Uart::println(sources[0].sample->fileName);
	        Uart::print("voices: ");
	        Uart::println(voiceCount);
	        Uart::print("reasons on clusters: ");
	        Uart::println(totalNumReasons);
	        Uart::print("Num voice unison part pointers to those clusters: ");
	        Uart::println(reasonCount);
	        Uart::print("Num source pointers to those clusters: ");
	        Uart::println(reasonCountSources);

	        char buffer[5];
	        strcpy(buffer, error);
	        buffer[0] = 'F';
	        FREEZE_WITH_ERROR(buffer);
	    }
	}
	*/
}

uint32_t Sound::getGlobalLFOPhaseIncrement() {
	LFOConfig& config = lfoConfig[LFO1_ID];
	if (config.syncLevel == SYNC_LEVEL_NONE) {
		return paramFinalValues[params::GLOBAL_LFO_FREQ - params::FIRST_GLOBAL];
	}
	else {
		return getSyncedLFOPhaseIncrement(config);
	}
}

uint32_t Sound::getSyncedLFOPhaseIncrement(const LFOConfig& config) {
	uint32_t phaseIncrement =
	    (playbackHandler.getTimePerInternalTickInverse()) >> (SYNC_LEVEL_256TH - config.syncLevel);
	switch (config.syncType) {
	case SYNC_TYPE_EVEN:
		// Nothing to do
		break;
	case SYNC_TYPE_TRIPLET:
		phaseIncrement = phaseIncrement * 3 / 2;
		break;
	case SYNC_TYPE_DOTTED:
		phaseIncrement = phaseIncrement * 2 / 3;
		break;
	}
	return phaseIncrement;
}

void Sound::resyncGlobalLFO() {
	if (!playbackHandler.isEitherClockActive()) {
		return; // no clock, no sync
	}
	// TOOD: convert to a guard clause. Now avoiding re-indenting the whole function.
	if (lfoConfig[LFO1_ID].syncLevel != SYNC_LEVEL_NONE) {

		timeStartedSkippingRenderingLFO =
		    AudioEngine::audioSampleTimer; // Resets the thing where the number of samples skipped is later converted
		                                   // into LFO phase increment

		globalLFO.setGlobalInitialPhase(lfoConfig[LFO1_ID]);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (!lastInternalTickDone && !timeSinceLastTick) {
			return;
		}

		uint32_t numInternalTicksPerPeriod = 3 << (SYNC_LEVEL_256TH - lfoConfig[LFO1_ID].syncLevel);
		switch (lfoConfig[LFO1_ID].syncType) {
		case SYNC_TYPE_EVEN:
			// Nothing to do
			break;
		case SYNC_TYPE_TRIPLET:
			numInternalTicksPerPeriod = numInternalTicksPerPeriod * 2 / 3;
			break;
		case SYNC_TYPE_DOTTED:
			numInternalTicksPerPeriod = numInternalTicksPerPeriod * 3 / 2;
			break;
		}
		uint32_t offsetTicks = (uint64_t)lastInternalTickDone % (uint16_t)numInternalTicksPerPeriod;

		// If we're right at a bar (or something), no need to do anyting else
		if (!timeSinceLastTick && !offsetTicks) {
			return;
		}

		uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();
		uint32_t timePerPeriod = numInternalTicksPerPeriod * timePerInternalTick;
		uint32_t offsetTime = offsetTicks * timePerInternalTick + timeSinceLastTick;
		globalLFO.phase += (uint32_t)((float)offsetTime / timePerPeriod * 4294967296);
	}
}

// ------------------------------------
// ModControllable implementation
// ------------------------------------

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool Sound::learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
                      uint8_t midiChannel, Song* song) {

	// If a mod knob
	if (midiChannel >= 16) {

		// If that knob was patched to something else...
		bool overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].paramDescriptor != paramDescriptor);

		modKnobs[modKnobMode][whichKnob].paramDescriptor = paramDescriptor;

		if (overwroteExistingKnob) {
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
		}

		return true;
	}

	// If a MIDI knob
	else {
		return ModControllableAudio::learnKnob(fromDevice, paramDescriptor, whichKnob, modKnobMode, midiChannel, song);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {

	// We gotta do this for any backedUpParamManagers too!
	int32_t i = song->backedUpParamManagers.search((uint32_t)(ModControllableAudio*)this,
	                                               GREATER_OR_EQUAL); // Search by first word only.

	while (true) {
		if (i >= song->backedUpParamManagers.getNumElements()) {
			break;
		}
		BackedUpParamManager* backedUp = (BackedUpParamManager*)song->backedUpParamManagers.getElementAddress(i);
		if (backedUp->modControllable != this) {
			break;
		}

		if (backedUp->clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, this, backedUp->clip,
			                                                   &backedUp->paramManager);
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
		}
		else {
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&backedUp->paramManager);
		}
		i++;
	}

	song->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(this); // What does this do exactly, again?
}

const uint8_t patchedParamsWhichShouldBeZeroIfNoKnobAssigned[] = {
    params::LOCAL_PITCH_ADJUST, params::LOCAL_OSC_A_PITCH_ADJUST, params::LOCAL_OSC_B_PITCH_ADJUST,
    params::LOCAL_MODULATOR_0_PITCH_ADJUST, params::LOCAL_MODULATOR_1_PITCH_ADJUST};

void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager) {

	for (int32_t i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		int32_t p = patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i];
		ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(paramManager, p);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack) {

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->paramManager->getPatchCableSet(modelStack);

	for (int32_t i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		ModelStackWithParamId* modelStackWithParamId =
		    modelStackWithParamCollection->addParamId(patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i]);
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(
		    modelStackWithParamId, false); // Don't allow creation
		if (modelStackWithAutoParam->autoParam) {
			ensureParamPresetValueWithoutKnobIsZero(modelStackWithAutoParam);
		}
	}
}

// Only worked for patched params
void Sound::ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack) {

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (modelStack->autoParam->isAutomated()) {
		return;
	}

	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(modelStack->paramId)) {
				return;
			}
		}
	}

	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor.isSetToParamWithNoSource(modelStack->paramId)) {
			return;
		}
	}

	// If we're here, no knobs were assigned to this param, so make it 0
	modelStack->autoParam->setCurrentValueWithNoReversionOrRecording(modelStack, 0);
}

void Sound::ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p) {

	AutoParam* param = &paramManager->getPatchedParamSet()->params[p];

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (param->isAutomated()) {
		return;
	}

	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(p)) {
				return;
			}
		}
	}

	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor.isSetToParamWithNoSource(p)) {
			return;
		}
	}

	// If we're here, no knobs were assigned to this param, so make it 0
	param->setCurrentValueBasicForSetup(0);
}

void Sound::doneReadingFromFile() {
	calculateEffectiveVolume();

	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].doneReadingFromFile(this);
	}

	setupUnisonDetuners(NULL);
	setupUnisonStereoSpread();

	for (int32_t m = 0; m < kNumModulators; m++) {
		recalculateModulatorTransposer(m, NULL);
	}
}

bool Sound::hasAnyVoices(bool resetTimeEntered) {
	return (numVoicesAssigned != 0);
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::voiceUnassigned(ModelStackWithVoice* modelStack) {

	numVoicesAssigned--;
	reassessRenderSkippingStatus(modelStack);
}

// modelStack may be NULL if no voices currently active
void Sound::setupUnisonDetuners(ModelStackWithSoundFlags* modelStack) {
	if (numUnison != 1) {
		int32_t detuneScaled = (int32_t)unisonDetune * 42949672;
		int32_t lowestVoice = -(detuneScaled >> 1);
		int32_t voiceSpacing = detuneScaled / (numUnison - 1);

		for (int32_t u = 0; u < numUnison; u++) {

			// Middle unison part gets no detune
			if ((numUnison & 1) && u == ((numUnison - 1) >> 1)) {
				unisonDetuners[u].setNoDetune();
			}
			else {
				unisonDetuners[u].setup(lowestVoice + voiceSpacing * u);
			}
		}
	}
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

void Sound::setupUnisonStereoSpread() {
	if (numUnison != 1) {
		int32_t spreadScaled = (int32_t)unisonStereoSpread * 42949672;
		int32_t lowestVoice = -(spreadScaled >> 1);
		int32_t voiceSpacing = spreadScaled / (numUnison - 1);

		for (int32_t u = 0; u < numUnison; u++) {
			// alternate the voices like -2 +1 0 -1 +2 for more balanced
			// interaction with detune
			bool isOdd = std::min(u, numUnison - 1 - u) & 1;
			int32_t sign = isOdd ? -1 : 1;

			unisonPan[u] = sign * (lowestVoice + voiceSpacing * u);
		}
	}
}

void Sound::calculateEffectiveVolume() {
	// volumeNeutralValueForUnison = (float)getParamNeutralValue(params::LOCAL_VOLUME) / sqrt(numUnison);
	volumeNeutralValueForUnison = (float)134217728 / sqrtf(numUnison);
}

// May change mod knob functions. You must update mod knob levels after calling this
void Sound::setSynthMode(SynthMode value, Song* song) {

	unassignAllVoices(); // This saves a lot of potential problems, to do with samples playing. E002 was being caused

	SynthMode oldSynthMode = synthMode;
	synthMode = value;
	setupPatchingForAllParamManagers(song);

	// Change mod knob functions over. Switching *to* FM...
	if (synthMode == SynthMode::FM && oldSynthMode != SynthMode::FM) {
		for (int32_t f = 0; f < kNumModButtons; f++) {
			if (modKnobs[f][0].paramDescriptor.isJustAParam() && modKnobs[f][1].paramDescriptor.isJustAParam()) {
				int32_t p0 = modKnobs[f][0].paramDescriptor.getJustTheParam();
				int32_t p1 = modKnobs[f][1].paramDescriptor.getJustTheParam();

				if ((p0 == params::LOCAL_LPF_RESONANCE || p0 == params::LOCAL_HPF_RESONANCE
				     || p0 == params::UNPATCHED_START + params::UNPATCHED_BASS)
				    && (p1 == params::LOCAL_LPF_FREQ || p1 == params::LOCAL_HPF_FREQ
				        || p1 == params::UNPATCHED_START + params::UNPATCHED_TREBLE)) {
					modKnobs[f][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_MODULATOR_1_VOLUME);
					modKnobs[f][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_MODULATOR_0_VOLUME);
				}
			}
		}
		// switch the filters off so they don't render unless deliberately enabled
		lpfMode = FilterMode::OFF;
		hpfMode = FilterMode::OFF;
	}

	// ... and switching *from* FM...
	if (synthMode != SynthMode::FM && oldSynthMode == SynthMode::FM) {
		for (int32_t f = 0; f < kNumModButtons; f++) {
			if (modKnobs[f][0].paramDescriptor.isSetToParamWithNoSource(params::LOCAL_MODULATOR_1_VOLUME)
			    && modKnobs[f][1].paramDescriptor.isSetToParamWithNoSource(params::LOCAL_MODULATOR_0_VOLUME)) {
				modKnobs[f][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);
				modKnobs[f][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
			}
		}
		// switch the filters back on if needed
		if (lpfMode == FilterMode::OFF) {
			lpfMode = FilterMode::TRANSISTOR_24DB;
		}
		if (hpfMode == FilterMode::OFF) {
			hpfMode = FilterMode::HPLADDER;
		}
	}
}

void Sound::setModulatorTranspose(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack) {
	modulatorTranspose[m] = value;
	recalculateAllVoicePhaseIncrements(modelStack);
}

void Sound::setModulatorCents(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack) {
	modulatorCents[m] = value;
	recalculateModulatorTransposer(m, modelStack);
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack) {
	modulatorTransposers[m].setup((int32_t)modulatorCents[m] * 42949672);
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack) {

	if (!numVoicesAssigned || !modelStack) {
		return; // These two "should" always be false in tandem...
	}

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);
		thisVoice->calculatePhaseIncrements(modelStackWithVoice);
	}
}

void Sound::setNumUnison(int32_t newNum, ModelStackWithSoundFlags* modelStack) {
	int32_t oldNum = numUnison;

	numUnison = newNum;
	setupUnisonDetuners(modelStack); // Can handle NULL. Also calls recalculateAllVoicePhaseIncrements()
	setupUnisonStereoSpread();
	calculateEffectiveVolume();

	// Effective volume has changed. Need to pass that change onto Voices
	if (numVoicesAssigned) {

		int32_t ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int32_t v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			if (synthMode == SynthMode::SUBTRACTIVE) {

				for (int32_t s = 0; s < kNumSources; s++) {

					bool sourceEverActive = modelStack->checkSourceEverActive(s);

					if (sourceEverActive && synthMode != SynthMode::FM && sources[s].oscType == OscType::SAMPLE
					    && thisVoice->guides[s].audioFileHolder && thisVoice->guides[s].audioFileHolder->audioFile) {

						// For samples, set the current play pos for the new unison part, if num unison went up
						if (newNum > oldNum) {

							VoiceUnisonPartSource* newPart = &thisVoice->unisonParts[oldNum].sources[s];
							VoiceUnisonPartSource* oldPart = &thisVoice->unisonParts[oldNum - 1].sources[s];

							newPart->active = oldPart->active;

							if (newPart->active) {
								newPart->oscPos = oldPart->oscPos;
								newPart->phaseIncrementStoredValue = oldPart->phaseIncrementStoredValue;
								newPart->carrierFeedback = oldPart->carrierFeedback;

								newPart->voiceSample = AudioEngine::solicitVoiceSample();
								if (!newPart->voiceSample) {
									newPart->active = false;
								}

								else {
									VoiceSample* newVoiceSample = newPart->voiceSample;
									VoiceSample* oldVoiceSample = oldPart->voiceSample;

									newVoiceSample->cloneFrom(
									    oldVoiceSample); // Just clones the SampleLowLevelReader stuff
									newVoiceSample->pendingSamplesLate = oldVoiceSample->pendingSamplesLate;

									newVoiceSample->doneFirstRenderYet = true;

									// Don't do any caching for new part. Old parts will stop using their cache anyway
									// because their pitch will have changed
									newVoiceSample->stopUsingCache(
									    &thisVoice->guides[s], (Sample*)thisVoice->guides[s].audioFileHolder->audioFile,
									    thisVoice->getPriorityRating(),
									    thisVoice->guides[s].getLoopingType(&sources[s]) == LoopType::LOW_LEVEL);
									// TODO: should really check success of that...
								}
							}
						}
						else if (newNum < oldNum) {
							for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {
								thisVoice->unisonParts[newNum].sources[s].unassign(false);
							}
						}
					}
				}
			}
		}
	}
}

void Sound::setUnisonDetune(int32_t newAmount, ModelStackWithSoundFlags* modelStack) {
	unisonDetune = newAmount;
	setupUnisonDetuners(modelStack); // Can handle NULL
}

void Sound::setUnisonStereoSpread(int32_t newAmount) {
	unisonStereoSpread = newAmount;
	setupUnisonStereoSpread();
}

bool Sound::anyNoteIsOn() {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings && arpSettings->mode != ArpMode::OFF) {
		return (getArp()->hasAnyInputNotesActive());
	}

	return numVoicesAssigned;
}

bool Sound::hasFilters() {
	return (lpfMode != FilterMode::OFF || hpfMode != FilterMode::OFF);
}

void Sound::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                               int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
}

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the
// defaultParams into it) arpSettings optional - no need if you're loading a new V2.0+ song where Instruments are all
// separate from Clips and won't store any arp stuff
Error Sound::readFromFile(Deserializer& reader, ModelStackWithModControllable* modelStack,
                          int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings) {

	modulatorTranspose[1] = 0;
	memset(oscRetriggerPhase, 0, sizeof(oscRetriggerPhase));
	memset(modulatorRetriggerPhase, 0, sizeof(modulatorRetriggerPhase));

	char const* tagName;

	ParamManagerForTimeline paramManager;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		Error result =
		    readTagFromFile(reader, tagName, &paramManager, readAutomationUpToPos, arpSettings, modelStack->song);
		if (result == Error::NONE) {}
		else if (result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		else {
			reader.exitTag(tagName);
		}
	}

	// old FM patches can have a filter mode saved in them even though it wouldn't have rendered at the time
	if (synthMode == SynthMode::FM && reader.getFirmwareVersion() < FirmwareVersion::community({1, 2, 0})) {
		hpfMode = FilterMode::OFF;
		lpfMode = FilterMode::OFF;
	}

	// If we actually got a paramManager, we can do resonance compensation on it
	if (paramManager.containsAnyMainParamCollections()) {
		if (smDeserializer.firmware_version < FirmwareVersion::official({1, 2, 0})) {
			compensateVolumeForResonance(modelStack->addParamManager(&paramManager));
		}

		possiblySetupDefaultExpressionPatching(&paramManager);

		// And, we file it with the Song
		modelStack->song->backUpParamManager(this, (Clip*)modelStack->getTimelineCounterAllowNull(), &paramManager,
		                                     true);
	}

	doneReadingFromFile();

	// Ensure all MIDI knobs reference correct volume...
	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		ensureKnobReferencesCorrectVolume(knob);
	}

	return Error::NONE;
}

Error Sound::createParamManagerForLoading(ParamManagerForTimeline* paramManager) {

	Error error = paramManager->setupWithPatching();
	if (error != Error::NONE) {
		return error;
	}

	initParams(paramManager);

	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_SIDECHAIN_SHAPE].setCurrentValueBasicForSetup(
	    2147483647); // Hmm, why this here? Obviously I had some reason...
	return Error::NONE;
}

void Sound::compensateVolumeForResonance(ModelStackWithThreeMainThings* modelStack) {

	// If it was an old-firmware file, we need to compensate for resonance
	if (smDeserializer.firmware_version < FirmwareVersion::official({1, 2, 0}) && synthMode != SynthMode::FM) {
		if (modelStack->paramManager->resonanceBackwardsCompatibilityProcessed) {
			return;
		}

		modelStack->paramManager->resonanceBackwardsCompatibilityProcessed = true;

		PatchedParamSet* patchedParams = modelStack->paramManager->getPatchedParamSet();

		int32_t compensation = interpolateTableSigned(patchedParams->getValue(params::LOCAL_LPF_RESONANCE) + 2147483648,
		                                              32, oldResonanceCompensation, 3);
		float compensationDB = (float)compensation / (1024 << 16);

		if (compensationDB > 0.1) {
			// Uart::print("compensating dB: ");
			// Uart::println((int32_t)(compensationDB * 100));
			patchedParams->shiftParamVolumeByDB(params::GLOBAL_VOLUME_POST_FX, compensationDB);
		}

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->paramManager->getPatchCableSet(modelStack);

		PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

		patchCableSet->setupPatching(
		    modelStackWithParamCollection); // So that we may then call doesParamHaveSomethingPatchedToIt(), below

		// If no LPF on, and resonance is at 50%, set it to 0%
		if (!patchCableSet->doesParamHaveSomethingPatchedToIt(params::LOCAL_LPF_FREQ)
		    && !patchedParams->params[params::LOCAL_LPF_FREQ].isAutomated()
		    && patchedParams->params[params::LOCAL_LPF_FREQ].getCurrentValue() >= 2147483602
		    && !patchedParams->params[params::LOCAL_LPF_RESONANCE].isAutomated()
		    && patchedParams->params[params::LOCAL_LPF_RESONANCE].getCurrentValue() <= 0
		    && patchedParams->params[params::LOCAL_LPF_RESONANCE].getCurrentValue() >= -23) {
			patchedParams->params[params::LOCAL_LPF_RESONANCE].currentValue = -2147483648;
		}
	}
}

// paramManager only required for old old song files
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
/**
 * Reads the parameters from the reader's current file into paramManager
 * stack usage would be unbounded if file contained infinite tags
 */
Error Sound::readSourceFromFile(Deserializer& reader, int32_t s, ParamManagerForTimeline* paramManager,
                                int32_t readAutomationUpToPos) {

	Source* source = &sources[s];

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "type")) {
			source->setOscType(stringToOscType(reader.readTagOrAttributeValue()));
			reader.exitTag("type");
		}
		else if (!strcmp(tagName, "phaseWidth")) {
			ENSURE_PARAM_MANAGER_EXISTS
			patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_PHASE_WIDTH + s,
			                         readAutomationUpToPos);
			reader.exitTag("phaseWidth");
		}
		else if (!strcmp(tagName, "volume")) {
			ENSURE_PARAM_MANAGER_EXISTS
			patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_VOLUME + s,
			                         readAutomationUpToPos);
			reader.exitTag("volume");
		}
		else if (!strcmp(tagName, "transpose")) {
			source->transpose = reader.readTagOrAttributeValueInt();
			reader.exitTag("transpose");
		}
		else if (!strcmp(tagName, "cents")) {
			source->cents = reader.readTagOrAttributeValueInt();
			reader.exitTag("cents");
		}
		else if (!strcmp(tagName, "loopMode")) {
			source->repeatMode = static_cast<SampleRepeatMode>(reader.readTagOrAttributeValueInt());
			source->repeatMode = std::min(source->repeatMode, static_cast<SampleRepeatMode>(kNumRepeatModes - 1));
			reader.exitTag("loopMode");
		}
		else if (!strcmp(tagName, "oscillatorSync")) {
			int32_t value = reader.readTagOrAttributeValueInt();
			oscillatorSync = (value != 0);
			reader.exitTag("oscillatorSync");
		}
		else if (!strcmp(tagName, "reversed")) {
			source->sampleControls.reversed = reader.readTagOrAttributeValueInt();
			reader.exitTag("reversed");
		}
		else if (!strcmp(tagName, "dx7patch")) {
			DxPatch* patch = source->ensureDxPatch();
			int len = reader.readTagOrAttributeValueHexBytes(patch->params, 156);
			reader.exitTag("dx7patch");
		}
		else if (!strcmp(tagName, "dx7randomdetune")) {
			DxPatch* patch = source->ensureDxPatch();
			patch->random_detune = reader.readTagOrAttributeValueInt();
			reader.exitTag("dx7randomdetune");
		}
		else if (!strcmp(tagName, "dx7enginemode")) {
			DxPatch* patch = source->ensureDxPatch();
			patch->setEngineMode(reader.readTagOrAttributeValueInt());
			reader.exitTag("dx7enginemode");
		}
		/*
		else if (!strcmp(tagName, "sampleSync")) {
		    source->sampleSync = stringToBool(reader.readTagContents());
		    reader.exitTag("sampleSync");
		}
		*/
		else if (!strcmp(tagName, "timeStretchEnable")) {
			source->sampleControls.pitchAndSpeedAreIndependent = reader.readTagOrAttributeValueInt();
			reader.exitTag("timeStretchEnable");
		}
		else if (!strcmp(tagName, "timeStretchAmount")) {
			source->timeStretchAmount = reader.readTagOrAttributeValueInt();
			reader.exitTag("timeStretchAmount");
		}
		else if (!strcmp(tagName, "linearInterpolation")) {
			if (reader.readTagOrAttributeValueInt()) {
				source->sampleControls.interpolationMode = InterpolationMode::LINEAR;
			}
			reader.exitTag("linearInterpolation");
		}
		else if (!strcmp(tagName, "retrigPhase")) {
			oscRetriggerPhase[s] = reader.readTagOrAttributeValueInt();
			reader.exitTag("retrigPhase");
		}
		else if (!strcmp(tagName, "fileName")) {

			MultiRange* range = source->getOrCreateFirstRange();
			if (!range) {
				return Error::INSUFFICIENT_RAM;
			}

			reader.readTagOrAttributeValueString(&range->getAudioFileHolder()->filePath);

			reader.exitTag("fileName");
		}
		else if (!strcmp(tagName, "zone")) {

			MultisampleRange* range = (MultisampleRange*)source->getOrCreateFirstRange();
			if (!range) {
				return Error::INSUFFICIENT_RAM;
			}

			range->sampleHolder.startMSec = 0;
			range->sampleHolder.endMSec = 0;
			range->sampleHolder.startPos = 0;
			range->sampleHolder.endPos = 0;

			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "startSeconds")) {
					range->sampleHolder.startMSec += reader.readTagOrAttributeValueInt() * 1000;
					reader.exitTag("startSeconds");
				}
				else if (!strcmp(tagName, "startMilliseconds")) {
					range->sampleHolder.startMSec += reader.readTagOrAttributeValueInt();
					reader.exitTag("startMilliseconds");
				}
				else if (!strcmp(tagName, "endSeconds")) {
					range->sampleHolder.endMSec += reader.readTagOrAttributeValueInt() * 1000;
					reader.exitTag("endSeconds");
				}
				else if (!strcmp(tagName, "endMilliseconds")) {
					range->sampleHolder.endMSec += reader.readTagOrAttributeValueInt();
					reader.exitTag("endMilliseconds");
				}

				else if (!strcmp(tagName, "startSamplePos")) {
					range->sampleHolder.startPos = reader.readTagOrAttributeValueInt();
					reader.exitTag("startSamplePos");
				}
				else if (!strcmp(tagName, "endSamplePos")) {
					range->sampleHolder.endPos = reader.readTagOrAttributeValueInt();
					reader.exitTag("endSamplePos");
				}

				else if (!strcmp(tagName, "startLoopPos")) {
					range->sampleHolder.loopStartPos = reader.readTagOrAttributeValueInt();
					reader.exitTag("startLoopPos");
				}
				else if (!strcmp(tagName, "endLoopPos")) {
					range->sampleHolder.loopEndPos = reader.readTagOrAttributeValueInt();
					reader.exitTag("endLoopPos");
				}

				else {
					reader.exitTag(tagName);
				}
			}
			reader.exitTag("zone");
		}
		else if (!strcmp(tagName, "sampleRanges") || !strcmp(tagName, "wavetableRanges")) {

			while (*(tagName = reader.readNextTagOrAttributeName())) {

				if (!strcmp(tagName, "sampleRange") || !strcmp(tagName, "wavetableRange")) {

					char tempMemory[source->ranges.elementSize];

					MultiRange* tempRange;
					if (source->oscType == OscType::WAVETABLE) {
						tempRange = new (tempMemory) MultiWaveTableRange();
					}
					else {
						tempRange = new (tempMemory) MultisampleRange();
					}

					AudioFileHolder* holder = tempRange->getAudioFileHolder();

					while (*(tagName = reader.readNextTagOrAttributeName())) {

						if (!strcmp(tagName, "fileName")) {
							reader.readTagOrAttributeValueString(&holder->filePath);
							reader.exitTag("fileName");
						}
						else if (!strcmp(tagName, "rangeTopNote")) {
							tempRange->topNote = reader.readTagOrAttributeValueInt();
							reader.exitTag("rangeTopNote");
						}
						else if (source->oscType != OscType::WAVETABLE) {
							if (!strcmp(tagName, "zone")) {

								while (*(tagName = reader.readNextTagOrAttributeName())) {
									if (!strcmp(tagName, "startSamplePos")) {
										((SampleHolder*)holder)->startPos = reader.readTagOrAttributeValueInt();
										reader.exitTag("startSamplePos");
									}
									else if (!strcmp(tagName, "endSamplePos")) {
										((SampleHolder*)holder)->endPos = reader.readTagOrAttributeValueInt();
										reader.exitTag("endSamplePos");
									}

									else if (!strcmp(tagName, "startLoopPos")) {
										((SampleHolderForVoice*)holder)->loopStartPos =
										    reader.readTagOrAttributeValueInt();
										reader.exitTag("startLoopPos");
									}
									else if (!strcmp(tagName, "endLoopPos")) {
										((SampleHolderForVoice*)holder)->loopEndPos =
										    reader.readTagOrAttributeValueInt();
										reader.exitTag("endLoopPos");
									}
									else {
										reader.exitTag(tagName);
									}
								}
								reader.exitTag("zone");
							}
							else if (!strcmp(tagName, "transpose")) {
								((SampleHolderForVoice*)holder)->transpose = reader.readTagOrAttributeValueInt();
								reader.exitTag("transpose");
							}
							else if (!strcmp(tagName, "cents")) {
								((SampleHolderForVoice*)holder)->cents = reader.readTagOrAttributeValueInt();
								reader.exitTag("cents");
							}
							else {
								goto justExitTag;
							}
						}
						else {
justExitTag:
							reader.exitTag(tagName);
						}
					}

					int32_t i = source->ranges.search(tempRange->topNote, GREATER_OR_EQUAL);
					Error error;

					// Ensure no duplicate topNote.
					if (i < source->ranges.getNumElements()) {
						MultisampleRange* existingRange = (MultisampleRange*)source->ranges.getElementAddress(i);
						if (existingRange->topNote == tempRange->topNote) {
							error = Error::FILE_CORRUPTED;
							goto gotError;
						}
					}

					error = source->ranges.insertAtIndex(i);
					if (error != Error::NONE) {
gotError:
						tempRange->~MultiRange();
						return error;
					}

					void* destinationRange = (MultisampleRange*)source->ranges.getElementAddress(i);
					memcpy(destinationRange, tempRange, source->ranges.elementSize);

					reader.exitTag();
				}
				else {
					reader.exitTag();
				}
			}

			reader.exitTag();
		}
		else {
			reader.exitTag();
		}
	}

	return Error::NONE;
}
#pragma GCC diagnostic pop

void Sound::writeSourceToFile(Serializer& writer, int32_t s, char const* tagName) {

	Source* source = &sources[s];

	writer.writeOpeningTagBeginning(tagName);

	if (synthMode != SynthMode::FM) {
		writer.writeAttribute("type", oscTypeToString(source->oscType));
	}

	// If (multi)sample...
	if (source->oscType == OscType::SAMPLE
	    && synthMode != SynthMode::FM) { // Don't combine this with the above "if" - there's an "else" below
		writer.writeAttribute("loopMode", util::to_underlying(source->repeatMode));
		writer.writeAttribute("reversed", source->sampleControls.reversed);
		writer.writeAttribute("timeStretchEnable", source->sampleControls.pitchAndSpeedAreIndependent);
		writer.writeAttribute("timeStretchAmount", source->timeStretchAmount);
		if (source->sampleControls.interpolationMode == InterpolationMode::LINEAR) {
			writer.writeAttribute("linearInterpolation", 1);
		}

		int32_t numRanges = source->ranges.getNumElements();

		if (numRanges > 1) {
			writer.writeOpeningTagEnd();
			writer.writeOpeningTag("sampleRanges");
		}

		for (int32_t e = 0; e < numRanges; e++) {
			MultisampleRange* range = (MultisampleRange*)source->ranges.getElement(e);

			if (numRanges > 1) {
				writer.writeOpeningTagBeginning("sampleRange");

				if (e != numRanges - 1) {
					writer.writeAttribute("rangeTopNote", range->topNote);
				}
			}

			writer.writeAttribute("fileName", range->sampleHolder.audioFile
			                                      ? range->sampleHolder.audioFile->filePath.get()
			                                      : range->sampleHolder.filePath.get());
			if (range->sampleHolder.transpose) {
				writer.writeAttribute("transpose", range->sampleHolder.transpose);
			}
			if (range->sampleHolder.cents) {
				writer.writeAttribute("cents", range->sampleHolder.cents);
			}

			writer.writeOpeningTagEnd();

			writer.writeOpeningTagBeginning("zone");
			writer.writeAttribute("startSamplePos", range->sampleHolder.startPos);
			writer.writeAttribute("endSamplePos", range->sampleHolder.endPos);
			if (range->sampleHolder.loopStartPos) {
				writer.writeAttribute("startLoopPos", range->sampleHolder.loopStartPos);
			}
			if (range->sampleHolder.loopEndPos) {
				writer.writeAttribute("endLoopPos", range->sampleHolder.loopEndPos);
			}
			writer.closeTag();

			if (numRanges > 1) {
				writer.writeClosingTag("sampleRange");
			}
		}

		if (numRanges > 1) {
			writer.writeClosingTag("sampleRanges");
		}
		else if (numRanges == 0) {
			writer.writeOpeningTagEnd();
		}

		writer.writeClosingTag(tagName);
	}

	// Otherwise, if we're *not* a (multi)sample, here's the other option, which includes (multi)wavetable
	else {
		writer.writeAttribute("transpose", source->transpose);
		writer.writeAttribute("cents", source->cents);
		if (s == 1 && oscillatorSync) {
			writer.writeAttribute("oscillatorSync", oscillatorSync);
		}
		writer.writeAttribute("retrigPhase", oscRetriggerPhase[s]);

		// Sub-option for (multi)wavetable
		if (source->oscType == OscType::WAVETABLE && synthMode != SynthMode::FM) {

			int32_t numRanges = source->ranges.getNumElements();

			if (numRanges > 1) {
				writer.writeOpeningTagEnd();
				writer.writeOpeningTag("wavetableRanges");
			}

			for (int32_t e = 0; e < numRanges; e++) {
				MultisampleRange* range = (MultisampleRange*)source->ranges.getElement(e);

				if (numRanges > 1) {
					writer.writeOpeningTagBeginning("wavetableRange");

					if (e != numRanges - 1) {
						writer.writeAttribute("rangeTopNote", range->topNote);
					}
				}

				writer.writeAttribute("fileName", range->sampleHolder.audioFile
				                                      ? range->sampleHolder.audioFile->filePath.get()
				                                      : range->sampleHolder.filePath.get());

				if (numRanges > 1) {
					writer.closeTag();
				}
			}

			if (numRanges > 1) {
				writer.writeClosingTag("wavetableRanges");
				writer.writeClosingTag(tagName);
			}
			else {
				goto justCloseTag;
			}
		}
		else if (source->oscType == OscType::DX7
		         && synthMode != SynthMode::FM) { // Don't combine this with the above "if" - there's an "else" below
			if (source->dxPatch) {
				DxPatch* patch = source->dxPatch;
				writer.writeAttributeHexBytes("dx7patch", patch->params, 156);

				if (patch->engineMode != 0) {
					writer.writeAttribute("dx7enginemode", patch->engineMode);
				}

				// real extension:
				if (patch->random_detune != 0) {
					writer.writeAttribute("dx7randomdetune", patch->random_detune);
				}
			}
			goto justCloseTag;
		}
		else {
justCloseTag:
			writer.closeTag();
		}
	}
}

bool Sound::readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
                                 int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;
	ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();
	PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "arpeggiatorGate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_GATE, readAutomationUpToPos);
		reader.exitTag("arpeggiatorGate");
	}
	else if (!strcmp(tagName, "ratchetProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetProbability");
	}
	else if (!strcmp(tagName, "ratchetAmount")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_AMOUNT,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetAmount");
	}
	else if (!strcmp(tagName, "sequenceLength")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SEQUENCE_LENGTH,
		                           readAutomationUpToPos);
		reader.exitTag("sequenceLength");
	}
	else if (!strcmp(tagName, "rhythm")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RHYTHM, readAutomationUpToPos);
		reader.exitTag("rhythm");
	}
	else if (!strcmp(tagName, "portamento")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PORTAMENTO, readAutomationUpToPos);
		reader.exitTag("portamento");
	}
	else if (!strcmp(tagName, "compressorShape")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_SHAPE,
		                           readAutomationUpToPos);
		reader.exitTag("compressorShape");
	}

	else if (!strcmp(tagName, "noiseVolume")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_NOISE_VOLUME, readAutomationUpToPos);
		reader.exitTag("noiseVolume");
	}
	else if (!strcmp(tagName, "oscAVolume")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_VOLUME, readAutomationUpToPos);
		reader.exitTag("oscAVolume");
	}
	else if (!strcmp(tagName, "oscBVolume")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_VOLUME, readAutomationUpToPos);
		reader.exitTag("oscBVolume");
	}
	else if (!strcmp(tagName, "oscAPulseWidth")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_PHASE_WIDTH, readAutomationUpToPos);
		reader.exitTag("oscAPulseWidth");
	}
	else if (!strcmp(tagName, "oscBPulseWidth")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_PHASE_WIDTH, readAutomationUpToPos);
		reader.exitTag("oscBPulseWidth");
	}
	else if (!strcmp(tagName, "oscAWavetablePosition")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_WAVE_INDEX, readAutomationUpToPos);
		reader.exitTag();
	}
	else if (!strcmp(tagName, "oscBWavetablePosition")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_WAVE_INDEX, readAutomationUpToPos);
		reader.exitTag();
	}
	else if (!strcmp(tagName, "volume")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_VOLUME_POST_FX, readAutomationUpToPos);
		reader.exitTag("volume");
	}
	else if (!strcmp(tagName, "pan")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_PAN, readAutomationUpToPos);
		reader.exitTag("pan");
	}
	else if (!strcmp(tagName, "lpfFrequency")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_FREQ, readAutomationUpToPos);
		reader.exitTag("lpfFrequency");
	}
	else if (!strcmp(tagName, "lpfResonance")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_RESONANCE, readAutomationUpToPos);
		reader.exitTag("lpfResonance");
	}
	else if (!strcmp(tagName, "lpfMorph")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LPF_MORPH, readAutomationUpToPos);
		reader.exitTag("lpfMorph");
	}
	else if (!strcmp(tagName, "hpfFrequency")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_FREQ, readAutomationUpToPos);
		reader.exitTag("hpfFrequency");
	}
	else if (!strcmp(tagName, "hpfResonance")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_RESONANCE, readAutomationUpToPos);
		reader.exitTag("hpfResonance");
	}
	else if (!strcmp(tagName, "hpfMorph")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_HPF_MORPH, readAutomationUpToPos);
		reader.exitTag("hpfMorph");
	}
	else if (!strcmp(tagName, "waveFold")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_FOLD, readAutomationUpToPos);
		reader.exitTag("waveFold");
	}

	else if (!strcmp(tagName, "envelope1")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_ATTACK,
				                         readAutomationUpToPos);
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_DECAY,
				                         readAutomationUpToPos);
				reader.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_SUSTAIN,
				                         readAutomationUpToPos);
				reader.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_0_RELEASE,
				                         readAutomationUpToPos);
				reader.exitTag("release");
			}
		}
		reader.exitTag("envelope1");
	}
	else if (!strcmp(tagName, "envelope2")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_ATTACK,
				                         readAutomationUpToPos);
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_DECAY,
				                         readAutomationUpToPos);
				reader.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_SUSTAIN,
				                         readAutomationUpToPos);
				reader.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_ENV_1_RELEASE,
				                         readAutomationUpToPos);
				reader.exitTag("release");
			}
		}
		reader.exitTag("envelope2");
	}
	else if (!strcmp(tagName, "lfo1Rate")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_LFO_FREQ, readAutomationUpToPos);
		reader.exitTag("lfo1Rate");
	}
	else if (!strcmp(tagName, "lfo2Rate")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_LFO_LOCAL_FREQ, readAutomationUpToPos);
		reader.exitTag("lfo2Rate");
	}
	else if (!strcmp(tagName, "modulator1Amount")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_0_VOLUME, readAutomationUpToPos);
		reader.exitTag("modulator1Amount");
	}
	else if (!strcmp(tagName, "modulator2Amount")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_1_VOLUME, readAutomationUpToPos);
		reader.exitTag("modulator2Amount");
	}
	else if (!strcmp(tagName, "modulator1Feedback")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_0_FEEDBACK,
		                         readAutomationUpToPos);
		reader.exitTag("modulator1Feedback");
	}
	else if (!strcmp(tagName, "modulator2Feedback")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_1_FEEDBACK,
		                         readAutomationUpToPos);
		reader.exitTag("modulator2Feedback");
	}
	else if (!strcmp(tagName, "carrier1Feedback")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_CARRIER_0_FEEDBACK, readAutomationUpToPos);
		reader.exitTag("carrier1Feedback");
	}
	else if (!strcmp(tagName, "carrier2Feedback")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_CARRIER_1_FEEDBACK, readAutomationUpToPos);
		reader.exitTag("carrier2Feedback");
	}
	else if (!strcmp(tagName, "pitchAdjust")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("pitchAdjust");
	}
	else if (!strcmp(tagName, "oscAPitchAdjust")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_A_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("oscAPitchAdjust");
	}
	else if (!strcmp(tagName, "oscBPitchAdjust")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_OSC_B_PITCH_ADJUST, readAutomationUpToPos);
		reader.exitTag("oscBPitchAdjust");
	}
	else if (!strcmp(tagName, "mod1PitchAdjust")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_0_PITCH_ADJUST,
		                         readAutomationUpToPos);
		reader.exitTag("mod1PitchAdjust");
	}
	else if (!strcmp(tagName, "mod2PitchAdjust")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::LOCAL_MODULATOR_1_PITCH_ADJUST,
		                         readAutomationUpToPos);
		reader.exitTag("mod2PitchAdjust");
	}
	else if (!strcmp(tagName, "modFXRate")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_MOD_FX_RATE, readAutomationUpToPos);
		reader.exitTag("modFXRate");
	}
	else if (!strcmp(tagName, "modFXDepth")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_MOD_FX_DEPTH, readAutomationUpToPos);
		reader.exitTag("modFXDepth");
	}
	else if (!strcmp(tagName, "delayRate")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_DELAY_RATE, readAutomationUpToPos);
		reader.exitTag("delayRate");
	}
	else if (!strcmp(tagName, "delayFeedback")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_DELAY_FEEDBACK, readAutomationUpToPos);
		reader.exitTag("delayFeedback");
	}
	else if (!strcmp(tagName, "reverbAmount")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_REVERB_AMOUNT, readAutomationUpToPos);
		reader.exitTag("reverbAmount");
	}
	else if (!strcmp(tagName, "arpeggiatorRate")) {
		patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_ARP_RATE, readAutomationUpToPos);
		reader.exitTag("arpeggiatorRate");
	}
	else if (!strcmp(tagName, "patchCables")) {
		paramManager->getPatchCableSet()->readPatchCablesFromFile(reader, readAutomationUpToPos);
		reader.exitTag("patchCables");
	}
	else if (ModControllableAudio::readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}

	else {
		return false;
	}

	return true;
}

void Sound::writeParamsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(writer, "arpeggiatorGate", params::UNPATCHED_ARP_GATE, writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "portamento", params::UNPATCHED_PORTAMENTO, writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "compressorShape", params::UNPATCHED_SIDECHAIN_SHAPE,
	                                       writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "oscAVolume", params::LOCAL_OSC_A_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "oscAPulseWidth", params::LOCAL_OSC_A_PHASE_WIDTH, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "oscAWavetablePosition", params::LOCAL_OSC_A_WAVE_INDEX,
	                                     writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "oscBVolume", params::LOCAL_OSC_B_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "oscBPulseWidth", params::LOCAL_OSC_B_PHASE_WIDTH, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "oscBWavetablePosition", params::LOCAL_OSC_B_WAVE_INDEX,
	                                     writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "noiseVolume", params::LOCAL_NOISE_VOLUME, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "volume", params::GLOBAL_VOLUME_POST_FX, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "pan", params::LOCAL_PAN, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "lpfFrequency", params::LOCAL_LPF_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "lpfResonance", params::LOCAL_LPF_RESONANCE, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "hpfFrequency", params::LOCAL_HPF_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "hpfResonance", params::LOCAL_HPF_RESONANCE, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "lfo1Rate", params::GLOBAL_LFO_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "lfo2Rate", params::LOCAL_LFO_LOCAL_FREQ, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "modulator1Amount", params::LOCAL_MODULATOR_0_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "modulator1Feedback", params::LOCAL_MODULATOR_0_FEEDBACK,
	                                     writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "modulator2Amount", params::LOCAL_MODULATOR_1_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "modulator2Feedback", params::LOCAL_MODULATOR_1_FEEDBACK,
	                                     writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "carrier1Feedback", params::LOCAL_CARRIER_0_FEEDBACK, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "carrier2Feedback", params::LOCAL_CARRIER_1_FEEDBACK, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "pitchAdjust", params::LOCAL_PITCH_ADJUST, writeAutomation, true);
	patchedParams->writeParamAsAttribute(writer, "oscAPitchAdjust", params::LOCAL_OSC_A_PITCH_ADJUST, writeAutomation,
	                                     true);
	patchedParams->writeParamAsAttribute(writer, "oscBPitchAdjust", params::LOCAL_OSC_B_PITCH_ADJUST, writeAutomation,
	                                     true);
	patchedParams->writeParamAsAttribute(writer, "mod1PitchAdjust", params::LOCAL_MODULATOR_0_PITCH_ADJUST,
	                                     writeAutomation, true);
	patchedParams->writeParamAsAttribute(writer, "mod2PitchAdjust", params::LOCAL_MODULATOR_1_PITCH_ADJUST,
	                                     writeAutomation, true);

	patchedParams->writeParamAsAttribute(writer, "modFXRate", params::GLOBAL_MOD_FX_RATE, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "modFXDepth", params::GLOBAL_MOD_FX_DEPTH, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "delayRate", params::GLOBAL_DELAY_RATE, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "delayFeedback", params::GLOBAL_DELAY_FEEDBACK, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "reverbAmount", params::GLOBAL_REVERB_AMOUNT, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "arpeggiatorRate", params::GLOBAL_ARP_RATE, writeAutomation);

	ModControllableAudio::writeParamAttributesToFile(writer, paramManager, writeAutomation);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)

	patchedParams->writeParamAsAttribute(writer, "lpfMorph", params::LOCAL_LPF_MORPH, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "hpfMorph", params::LOCAL_HPF_MORPH, writeAutomation);

	patchedParams->writeParamAsAttribute(writer, "waveFold", params::LOCAL_FOLD, writeAutomation);

	unpatchedParams->writeParamAsAttribute(writer, "ratchetProbability", params::UNPATCHED_ARP_RATCHET_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "ratchetAmount", params::UNPATCHED_ARP_RATCHET_AMOUNT,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "sequenceLength", params::UNPATCHED_ARP_SEQUENCE_LENGTH,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "rhythm", params::UNPATCHED_ARP_RHYTHM, writeAutomation);

	writer.writeOpeningTagEnd();

	// Envelopes
	writer.writeOpeningTagBeginning("envelope1");
	patchedParams->writeParamAsAttribute(writer, "attack", params::LOCAL_ENV_0_ATTACK, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "decay", params::LOCAL_ENV_0_DECAY, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "sustain", params::LOCAL_ENV_0_SUSTAIN, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "release", params::LOCAL_ENV_0_RELEASE, writeAutomation);
	writer.closeTag();

	writer.writeOpeningTagBeginning("envelope2");
	patchedParams->writeParamAsAttribute(writer, "attack", params::LOCAL_ENV_1_ATTACK, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "decay", params::LOCAL_ENV_1_DECAY, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "sustain", params::LOCAL_ENV_1_SUSTAIN, writeAutomation);
	patchedParams->writeParamAsAttribute(writer, "release", params::LOCAL_ENV_1_RELEASE, writeAutomation);
	writer.closeTag();

	paramManager->getPatchCableSet()->writePatchCablesToFile(writer, writeAutomation);

	ModControllableAudio::writeParamTagsToFile(writer, paramManager, writeAutomation);
}

void Sound::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager,
                        ArpeggiatorSettings* arpSettings, const char* pathAttribute) {

	writer.writeAttribute("polyphonic", polyphonyModeToString(polyphonic));
	writer.writeAttribute("voicePriority", util::to_underlying(voicePriority));

	// Send level
	if (sideChainSendLevel != 0) {
		writer.writeAttribute("sideChainSend", sideChainSendLevel);
	}

	writer.writeAttribute("mode", (char*)synthModeToString(synthMode));

	if (transpose != 0) {
		writer.writeAttribute("transpose", transpose);
	}

	ModControllableAudio::writeAttributesToFile(writer);

	// Community Firmware parameters (always write them after the official ones)
	if (pathAttribute) {
		writer.writeAttribute("path", pathAttribute);
	}
	writer.writeAttribute("maxVoices", maxVoiceCount);

	writer.writeOpeningTagEnd(); // -------------------------------------------------------------------------

	writeSourceToFile(writer, 0, "osc1");
	writeSourceToFile(writer, 1, "osc2");

	// LFOs
	writer.writeOpeningTagBeginning("lfo1");
	writer.writeAttribute("type", lfoTypeToString(lfoConfig[LFO1_ID].waveType), false);
	writer.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", lfoConfig[LFO1_ID].syncLevel, false);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeSyncTypeToFile(currentSong, "syncType", lfoConfig[LFO1_ID].syncType, false);
	writer.closeTag();

	writer.writeOpeningTagBeginning("lfo2");
	writer.writeAttribute("type", lfoTypeToString(lfoConfig[LFO2_ID].waveType), false);
	// Community Firmware parameters
	writer.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", lfoConfig[LFO2_ID].syncLevel, false);
	writer.writeSyncTypeToFile(currentSong, "syncType", lfoConfig[LFO2_ID].syncType, false);
	writer.closeTag();

	if (synthMode == SynthMode::FM) {

		writer.writeOpeningTagBeginning("modulator1");
		writer.writeAttribute("transpose", modulatorTranspose[0]);
		writer.writeAttribute("cents", modulatorCents[0]);
		writer.writeAttribute("retrigPhase", modulatorRetriggerPhase[0]);
		writer.closeTag();

		writer.writeOpeningTagBeginning("modulator2");
		writer.writeAttribute("transpose", modulatorTranspose[1]);
		writer.writeAttribute("cents", modulatorCents[1]);
		writer.writeAttribute("retrigPhase", modulatorRetriggerPhase[1]);
		writer.writeAttribute("toModulator1", modulator1ToModulator0);
		writer.closeTag();
	}

	writer.writeOpeningTagBeginning("unison");
	writer.writeAttribute("num", numUnison, false);
	writer.writeAttribute("detune", unisonDetune, false);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeAttribute("spread", unisonStereoSpread, false);
	writer.closeTag();

	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		Sound::writeParamsToFile(writer, paramManager, false);
		writer.writeClosingTag("defaultParams");
	}

	if (arpSettings) {
		writer.writeOpeningTagBeginning("arpeggiator");
		writer.writeAttribute("mode", arpPresetToOldArpMode(arpSettings->preset)); // For backwards compatibility
		writer.writeAttribute("numOctaves", arpSettings->numOctaves);
		writer.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", arpSettings->syncLevel, true);
		writer.writeSyncTypeToFile(currentSong, "syncType", arpSettings->syncType, true);
		writer.writeAttribute("arpMode", arpModeToString(arpSettings->mode));
		writer.writeAttribute("noteMode", arpNoteModeToString(arpSettings->noteMode));
		writer.writeAttribute("octaveMode", arpOctaveModeToString(arpSettings->octaveMode));
		writer.writeAttribute("mpeVelocity", arpMpeModSourceToString(arpSettings->mpeVelocity));
		writer.closeTag();
	}

	// Mod knobs
	writer.writeOpeningTag("modKnobs");
	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			ModKnob* knob = &modKnobs[k][w];
			writer.writeOpeningTagBeginning("modKnob");
			writer.writeAttribute(
			    "controlsParam",
			    params::paramNameForFile(params::Kind::UNPATCHED_SOUND, knob->paramDescriptor.getJustTheParam()),
			    false);
			if (!knob->paramDescriptor.isJustAParam()) {
				writer.writeAttribute("patchAmountFromSource",
				                      sourceToString(knob->paramDescriptor.getTopLevelSource()), false);

				if (knob->paramDescriptor.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource",
					                      sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}
			writer.closeTag();
		}
	}
	writer.writeClosingTag("modKnobs");

	ModControllableAudio::writeTagsToFile(writer);
}

int16_t Sound::getMaxOscTranspose(InstrumentClip* clip) {

	int32_t maxRawOscTranspose = -32768;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE) {
			maxRawOscTranspose = std::max<int32_t>(maxRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)modulatorTranspose[0]);
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)modulatorTranspose[1]);
	}

	if (maxRawOscTranspose == -32768) {
		maxRawOscTranspose = 0;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings(clip);

	if (arpSettings && arpSettings->mode != ArpMode::OFF) {
		maxRawOscTranspose += (arpSettings->numOctaves - 1) * 12;
	}

	return maxRawOscTranspose + transpose;
}

int16_t Sound::getMinOscTranspose() {

	int32_t minRawOscTranspose = 32767;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE) {
			minRawOscTranspose = std::min<int32_t>(minRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)modulatorTranspose[0]);
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)modulatorTranspose[1]);
	}

	if (minRawOscTranspose == 32767) {
		minRawOscTranspose = 0;
	}

	return minRawOscTranspose + transpose;
}

// Returns true if more loading needed later
Error Sound::loadAllAudioFiles(bool mayActuallyReadFiles) {

	for (int32_t s = 0; s < kNumSources; s++) {
		if (sources[s].oscType == OscType::SAMPLE || sources[s].oscType == OscType::WAVETABLE) {
			Error error = sources[s].loadAllSamples(mayActuallyReadFiles);
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	return Error::NONE;
}

bool Sound::envelopeHasSustainCurrently(int32_t e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	// These params are fetched "pre-LPF"
	return (patchedParams->getValue(params::LOCAL_ENV_0_SUSTAIN + e) != -2147483648
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

bool Sound::envelopeHasSustainEver(int32_t e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	return (patchedParams->params[params::LOCAL_ENV_0_SUSTAIN + e].containsSomething(-2147483648)
	        || patchedParams->params[params::LOCAL_ENV_0_DECAY + e].isAutomated()
	        || patchedParams->params[params::LOCAL_ENV_0_RELEASE + e].isAutomated()
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

void Sound::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	endStutter(paramManager);

	int32_t modKnobMode = *getModKnobMode();

	ModKnob* ourModKnobTop = &modKnobs[modKnobMode][1];
	ModKnob* ourModKnobBottom = &modKnobs[modKnobMode][0];

	// mod button popup logic
	// if top knob == LPF Freq && bottom knob == LPF Reso
	// if top knob == HPF Freq && bottom knob == HPF Reso
	// if top knob == Treble && bottom knob == Bass
	// --> displayFilterSettings(on, currentFilterType);

	// if top knob == Delay Rate && bottom knob == Delay Amount
	// --> displayDelaySettings(on);

	// if top knob == Sidechain && bottom knob == Reverb Amount
	// --> displaySidechainAndReverbSettings(on);

	// else --> display param name

	if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_FREQ)
	    && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_RESONANCE)) {
		displayFilterSettings(on, FilterType::LPF);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_FREQ)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_RESONANCE)) {
		displayFilterSettings(on, FilterType::HPF);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START + params::UNPATCHED_TREBLE)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START
	                                                                       + params::UNPATCHED_BASS)) {
		displayFilterSettings(on, FilterType::EQ);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_RATE)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_FEEDBACK)) {
		displayDelaySettings(on);
	}
	else if ((ourModKnobTop->paramDescriptor.hasJustOneSource()
	          && ourModKnobTop->paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_REVERB_AMOUNT)) {
		displaySidechainAndReverbSettings(on);
	}
	else {
		displayOtherModKnobSettings(whichModButton, on);
	}
}

ModelStackWithAutoParam* Sound::getParamFromModEncoder(int32_t whichModEncoder,
                                                       ModelStackWithThreeMainThings* modelStack, bool allowCreation) {

	// If setting up a macro by holding its encoder down, the knobs will represent macro control-amounts rather than
	// actual "params", so there's no "param".
	if (isUIModeActive(UI_MODE_MACRO_SETTING_UP)) {
		return modelStack->addParam(NULL, NULL, 0, NULL); // "none"
	}
	return getParamFromModEncoderDeeper(whichModEncoder, modelStack, allowCreation);
}

ModelStackWithAutoParam* Sound::getParamFromModEncoderDeeper(int32_t whichModEncoder,
                                                             ModelStackWithThreeMainThings* modelStack,
                                                             bool allowCreation) {

	int32_t paramId;
	ParamCollectionSummary* summary;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	int32_t modKnobMode = *getModKnobMode();
	ModKnob* knob = &modKnobs[modKnobMode][whichModEncoder];

	if (knob->paramDescriptor.isJustAParam()) {
		int32_t p = knob->paramDescriptor.getJustTheParam();

		// Unpatched param
		if (p >= params::UNPATCHED_START) {
			paramId = p - params::UNPATCHED_START;
			summary = paramManager->getUnpatchedParamSetSummary();
		}

		// Patched param
		else {
			paramId = p;
			summary = paramManager->getPatchedParamSetSummary();
		}
	}

	// Patch cable
	else {
		paramId = knob->paramDescriptor.data;
		summary = paramManager->getPatchCableSetSummary();
	}

	ModelStackWithParamId* newModelStack1 =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);
	return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}

bool Sound::modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {

	int32_t modKnobMode = *getModKnobMode();

	ModKnob* ourModKnob = &modKnobs[modKnobMode][whichModEncoder];

	if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START
	                                                         + params::UNPATCHED_STUTTER_RATE)) {
		if (on) {
			beginStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		else {
			endStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		reassessRenderSkippingStatus(modelStack->addSoundFlags());

		return false;
	}

	// Switch delay pingpong
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_RATE)) {
		if (on) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
			    == RuntimeFeatureStateToggle::On) {
				switchDelaySyncType();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelaySyncTypeDisplayName());
				}
			}
			else {
				switchDelayPingPong();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelayPingPongStatusDisplayName());
				}
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switch delay analog sim
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_FEEDBACK)) {
		if (on) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
			    == RuntimeFeatureStateToggle::On) {
				switchDelaySyncLevel();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					char displayName[30];
					getDelaySyncLevelDisplayName(displayName);
					display->displayPopup(displayName);
				}
			}
			else {
				switchDelayAnalog();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelayTypeDisplayName());
				}
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switch LPF mode
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_RESONANCE)) {
		if (on) {
			switchLPFMode();
			FilterType currentFilterType = FilterType::LPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterModeDisplayName(currentFilterType));
			}
			return true;
		}
		else {
			return false;
		}
	}
	// Switch HPF mode
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_RESONANCE)) {
		if (on) {
			switchHPFMode();
			FilterType currentFilterType = FilterType::HPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterModeDisplayName(currentFilterType));
			}
			return true;
		}
		else {
			return false;
		}
	}
	// Cycle through reverb presets
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_REVERB_AMOUNT)) {
		if (on) {
			view.cycleThroughReverbPresets();

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displaySidechainAndReverbSettings(on);
			}
			else {
				display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
			}
		}
		return false;
	}

	// Switch sidechain sync level
	else if (ourModKnob->paramDescriptor.hasJustOneSource()
	         && ourModKnob->paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN) {
		if (on) {
			int32_t insideWorldTickMagnitude;
			if (currentSong) { // Bit of a hack just referring to currentSong in here...
				insideWorldTickMagnitude =
				    (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
			}
			else {
				insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
			}

			if (sidechain.syncLevel == (SyncLevel)(7 - insideWorldTickMagnitude)) {
				sidechain.syncLevel = (SyncLevel)(9 - insideWorldTickMagnitude);
			}
			else {
				sidechain.syncLevel = (SyncLevel)(7 - insideWorldTickMagnitude);
			}

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displaySidechainAndReverbSettings(on);
			}
			else {
				display->displayPopup(getSidechainDisplayName());
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switching between LPF, HPF and EQ
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_FREQ)) {
		if (on && synthMode != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::LOCAL_HPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::LOCAL_LPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    params::LOCAL_HPF_RESONANCE);
			}
			FilterType currentFilterType = FilterType::HPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_FREQ)) {
		if (on && synthMode != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_TREBLE);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::LOCAL_HPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START
				                                                                              + params::UNPATCHED_BASS);
			}
			FilterType currentFilterType = FilterType::EQ;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START + params::UNPATCHED_TREBLE)) {
		if (on && synthMode != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::UNPATCHED_START + params::UNPATCHED_BASS)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    params::LOCAL_LPF_RESONANCE);
			}
			FilterType currentFilterType = FilterType::LPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	return false;
}

// modelStack may be NULL
void Sound::fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack) {
	if (!numVoicesAssigned) {
		return;
	}

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		bool stillGoing = thisVoice->doFastRelease();

		if (!stillGoing) {
			AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E212");
			AudioEngine::unassignVoice(thisVoice, this, modelStack); // Accepts NULL
			v--;
			ends[1]--;
		}
	}
}

void Sound::prepareForHibernation() {
	wontBeRenderedForAWhile();
	detachSourcesFromAudioFiles();
}

// This can get called either for hibernation, or because drum now has no active noteRow
void Sound::wontBeRenderedForAWhile() {
	ModControllableAudio::wontBeRenderedForAWhile();

	unassignAllVoices(); // Can't remember if this is always necessary, but it is when this is called from
	                     // Instrumentclip::detachFromInstrument()

	getArp()->reset(); // Surely this shouldn't be quite necessary?
	sidechain.status = EnvelopeStage::OFF;

	// Tell it to just cut the MODFX tail - we needa change status urgently!
	reassessRenderSkippingStatus(NULL, true);

	// If it still thinks it's meant to be rendering, we did something wrong
	if (ALPHA_OR_BETA_VERSION && !skippingRendering) {
		FREEZE_WITH_ERROR("E322");
	}
}

void Sound::detachSourcesFromAudioFiles() {
	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].detachAllAudioFiles();
	}
}

void Sound::deleteMultiRange(int32_t s, int32_t r) {
	// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned
	// during memory allocation
	unassignAllVoices();
	AudioEngine::audioRoutineLocked = true;
	sources[s].ranges.getElement(r)->~MultiRange();
	sources[s].ranges.deleteAtIndex(r);
	AudioEngine::audioRoutineLocked = false;
}

// This function has to give the same outcome as Source::renderInStereo()
bool Sound::renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack) {

	// audioDriver deciding we're rendering in mono overrides everything
	if (!AudioEngine::renderInStereo) {
		return false;
	}

	if (!numVoicesAssigned) {
		return false;
	}

	// Stereo live-input
	if ((sources[0].oscType == OscType::INPUT_STEREO || sources[1].oscType == OscType::INPUT_STEREO)
	    && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) {
		return true;
	}

	if (modelStack->paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_PAN)) {
		return true;
	}

	if (unisonStereoSpread && numUnison > 1) {
		return true;
	}

	uint32_t mustExamineSourceInEachVoice = 0;

	// Have a look at what samples, if any, are in each Source
	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sources[s];

		if (!modelStack->checkSourceEverActive(s)) {
			continue;
		}

		if (source->oscType == OscType::SAMPLE) { // Just SAMPLE, because WAVETABLEs can't be stereo.

			int32_t numRanges = source->ranges.getNumElements();

			// If multiple ranges, we have to come back and examine Voices to see which are in use
			if (numRanges > 1) {
				mustExamineSourceInEachVoice |= (1 << s);
			}

			// Or if just 1 range, we can examine it now
			else if (numRanges == 1) {
				MultiRange* range = source->ranges.getElement(0);
				AudioFileHolder* holder = range->getAudioFileHolder();

				if (holder->audioFile && holder->audioFile->numChannels == 2) {
					return true;
				}
			}
		}
	}

	// Ok, if that determined that either source has multiple samples (multisample ranges), we now have to
	// investigate each Voice
	if (mustExamineSourceInEachVoice) {

		int32_t ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int32_t v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			for (int32_t s = 0; s < kNumSources; s++) {
				if (mustExamineSourceInEachVoice & (1 << s)) {
					AudioFileHolder* holder = thisVoice->guides[s].audioFileHolder;
					if (holder && holder->audioFile && holder->audioFile->numChannels == 2) {
						return true;
					}
				}
			}
		}
	}

	// No stereo stuff found - we're rendering in mono.
	return false;
}

ModelStackWithAutoParam* Sound::getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary;
	int32_t paramId;

	if (knob->paramDescriptor.isJustAParam()) {

		int32_t p = knob->paramDescriptor.getJustTheParam();

		// Unpatched parameter
		if (p >= params::UNPATCHED_START) {
			return ModControllableAudio::getParamFromMIDIKnob(knob, modelStack);
		}

		// Actual (patched) parameter
		else {
			summary = modelStack->paramManager->getPatchedParamSetSummary();
			paramId = p;
		}
	}

	// Patch cable strength
	else {
		summary = modelStack->paramManager->getPatchCableSetSummary();
		paramId = knob->paramDescriptor.data;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId, true); // Allow patch cable creation. TODO: think this through better...

	return modelStackWithAutoParam;
}

/*

int32_t startV, endV;
audioDriver.voices.getRangeForSound(this, &startV, &endV);
for (int32_t v = startV; v < endV; v++) {
    Voice* thisVoice = audioDriver.voices.getElement(v)->voice;
}


for (int32_t s = 0; s < NUM_SOURCES; s++) {
*/
#pragma GCC diagnostic pop
