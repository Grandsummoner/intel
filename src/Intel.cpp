#include "plugin.hpp"
#include "IntelLink.hpp"
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------
// Small reusable DSP building blocks
// ---------------------------------------------------------------------

// One-pole lowpass, used as the "TONE" damping control in both the delay
// feedback path and the reverb's comb feedback paths.
struct OnePoleLP {
	float z = 0.f;
	float process(float in, float coeff) {
		z += coeff * (in - z);
		return z;
	}
};

// Single feedback comb filter (Freeverb-style), one per reverb "voice".
struct Comb {
	std::vector<float> buf;
	int idx = 0;
	OnePoleLP damp;
	void setDelaySamples(int n) {
		buf.assign(std::max(1, n), 0.f);
		idx = 0;
	}
	float process(float in, float feedback, float dampCoeff) {
		float out = buf[idx];
		float damped = damp.process(out, dampCoeff);
		buf[idx] = in + damped * feedback;
		idx = (idx + 1) % (int)buf.size();
		return out;
	}
};

// Series allpass diffuser, also Freeverb-style.
struct Allpass {
	std::vector<float> buf;
	int idx = 0;
	void setDelaySamples(int n) {
		buf.assign(std::max(1, n), 0.f);
		idx = 0;
	}
	float process(float in, float g) {
		float bufout = buf[idx];
		float out = -in + bufout;
		buf[idx] = in + bufout * g;
		idx = (idx + 1) % (int)buf.size();
		return out;
	}
};

// A very small granular octave-up shifter: two read heads into the same
// write buffer, each advancing at 2x the write rate (i.e. reading through
// history twice as fast = pitched up an octave), crossfaded with a
// triangular window spaced half a grain apart so the seam where one head
// wraps is masked by the other head's fade. Not a polished pitch-shift
// algorithm -- a deliberately simple one that's real and audibly gives a
// shimmer-style rising octave layer, not a stand-in.
struct OctaveUpShifter {
	std::vector<float> buf;
	int writeIdx = 0;
	float readPos[2] = {0.f, 0.f};
	float grainSamples = 2048.f;

	void init(int sampleRate) {
		buf.assign(sampleRate, 0.f); // 1 second of history is plenty
		grainSamples = sampleRate * 0.05f; // 50ms grains
		readPos[0] = 0.f;
		readPos[1] = grainSamples * 0.5f;
	}
	float readInterp(float pos) {
		int n = (int)buf.size();
		int i0 = ((int)pos % n + n) % n;
		int i1 = (i0 + 1) % n;
		float frac = pos - std::floor(pos);
		return buf[i0] * (1.f - frac) + buf[i1] * frac;
	}
	float process(float in) {
		buf[writeIdx] = in;
		writeIdx = (writeIdx + 1) % (int)buf.size();
		float out = 0.f;
		for (int h = 0; h < 2; h++) {
			float distIntoGrain = std::fmod(readPos[h], grainSamples);
			float win = std::sin(M_PI * distIntoGrain / grainSamples); // triangular-ish window
			win = win * win;
			out += readInterp((float)writeIdx - readPos[h]) * win;
			// advance at 2x speed -> reads history twice as fast -> octave up
			readPos[h] += 2.f;
			if (readPos[h] >= (float)buf.size() - 4.f) readPos[h] -= grainSamples;
		}
		return out * 0.7f;
	}
};

// ---------------------------------------------------------------------
// LFO / trigger channel
// ---------------------------------------------------------------------

struct LfoChannel {
	double phase = 0.0;
	int rateDivState = 1; // 0..3, higher = faster. With Beams' clock linked: fires every 8th / 4th / 2nd / every master tick. Unlinked: free-running at SPEED x {1,2,4,8}. Fill-bar convention, same as depth
	int depthState = 2;   // 0 = off, 1 = low, 2 = med, 3 = high -- (v6) governs ONLY the modulation sent to Command; no longer touches the triggers
	dsp::SchmittTrigger rateDivBtn, depthBtn;
	dsp::PulseGenerator trigPulse;
	float modValue = 0.f;
	int tickCount = 0;

	static constexpr float divMult[4] = {1.f, 2.f, 4.f, 8.f};            // free-running fallback
	static constexpr int tickDiv[4] = {8, 4, 2, 1};                      // master ticks per trigger
	static constexpr float modDepth[4] = {0.f, 0.15f, 0.35f, 0.6f};

	void handleButtons(float rateDivRaw, float depthRaw) {
		if (rateDivBtn.process(rateDivRaw)) rateDivState = (rateDivState + 1) % 4;
		if (depthBtn.process(depthRaw)) depthState = (depthState + 1) % 4;
	}

	// ticks > 0 means Beams' master clock ticked this sample AND is linked
	// (tickDriven); otherwise triggers free-run off the global Speed.
	void step(float sampleRate, float globalSpeedHz, bool tickDriven, int ticks) {
		double freq = globalSpeedHz * divMult[rateDivState];
		phase += freq / sampleRate;
		bool wrapped = false;
		if (phase >= 1.0) {
			phase -= 1.0;
			wrapped = true;
		}
		if (tickDriven) {
			for (int k = 0; k < ticks; k++) {
				if (++tickCount >= tickDiv[rateDivState]) {
					tickCount = 0;
					trigPulse.trigger(1e-3f);
				}
			}
		}
		else if (wrapped) {
			trigPulse.trigger(1e-3f);
		}
		modValue = std::sin(2.0 * M_PI * phase) * modDepth[depthState];
	}

	float trigVoltage(float sampleTime) {
		return trigPulse.process(sampleTime) ? 10.f : 0.f;
	}
};
constexpr int LfoChannel::tickDiv[4];
constexpr float LfoChannel::divMult[4];
constexpr float LfoChannel::modDepth[4];

// ---------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------

struct Intel : Module {
	enum ParamIds {
		RATEDIV1_PARAM, DEPTH1_PARAM,
		RATEDIV2_PARAM, DEPTH2_PARAM,
		RATEDIV3_PARAM, DEPTH3_PARAM,
		RATEDIV4_PARAM, DEPTH4_PARAM,
		DELAY_PARAM, REVERB_PARAM, SHIMMER_PARAM, SYNC_PARAM,
		MIX_PARAM, TIME_PARAM, REGEN_PARAM, TONE_PARAM, SPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds { EXT_L_INPUT, EXT_R_INPUT, NUM_INPUTS };
	enum OutputIds {
		MASTER_L_OUTPUT, MASTER_R_OUTPUT,
		TRIG1_OUTPUT, TRIG2_OUTPUT, TRIG3_OUTPUT, TRIG4_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LINK_LEFT_LIGHT, LINK_RIGHT_LIGHT,
		DELAY_LIGHT, REVERB_LIGHT, SHIMMER_LIGHT, SYNC_LIGHT,
		ENUMS(RATEDIV1_LIGHT, 1), ENUMS(DEPTH1_LIGHT, 1),
		ENUMS(RATEDIV2_LIGHT, 1), ENUMS(DEPTH2_LIGHT, 1),
		ENUMS(RATEDIV3_LIGHT, 1), ENUMS(DEPTH3_LIGHT, 1),
		ENUMS(RATEDIV4_LIGHT, 1), ENUMS(DEPTH4_LIGHT, 1),
		ENUMS(METER_LIGHT, 1),
		NUM_LIGHTS
	};

	// --- LFO channels: 0=RATE, 1=DENS, 2=SWING, 3=ENTROPY (fixed mapping to Command) ---
	LfoChannel lfo[4];
	dsp::SchmittTrigger delayPageBtn, reverbPageBtn, shimmerBtn, syncBtnTrig;
	bool delayPageSelected = true; // which page the shared knobs currently show
	bool shimmerOn = false;
	bool delaySync = false, reverbSync = false;

	// Per-effect stored parameter pages (knob-paging model)
	struct FxPage { float mix = 0.35f, time = 0.3f, regen = 0.4f, tone = 0.5f; };
	FxPage delayPage, reverbPage;
	float meterMs = 0.f;    // mean-square (RMS^2) follower, 1.0 = 10V peak = Rack's clip level
	int meterDiv = 0;
	bool firstProcess = true;

	// Delay line
	std::vector<float> delayBufL, delayBufR;
	int delayWriteIdx = 0;
	OnePoleLP delayToneL, delayToneR;

	// Reverb: 4 combs + 2 allpass per channel, classic Freeverb-ish tuning
	Comb combL[4], combR[4];
	Allpass apL[2], apR[2];
	OctaveUpShifter shimmerL, shimmerR;

	float sr = 44100.f;

	// Expander link to Command (invisible; no jacks). See IntelLink.hpp for
	// why this exact struct is duplicated verbatim in the Command repo.
	IntelModMessage leftProducer, leftConsumer, rightProducer, rightConsumer;
	unsigned int lastBeamsTick = 0;
	bool beamsTickSeen = false;

	Intel() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 4; i++) {
			configParam(RATEDIV1_PARAM + i * 2, 0.f, 1.f, 0.f, string::f("Trigger %d rate: with Beams linked, every 8th / 4th / 2nd / every master clock tick; otherwise free-running off SPEED", i + 1));
			configParam(DEPTH1_PARAM + i * 2, 0.f, 1.f, 0.f, string::f("Trigger %d modulation depth for Command (off / low / medium / high) -- does not affect the trigger itself", i + 1));
		}
		configParam(DELAY_PARAM, 0.f, 1.f, 0.f, "Show DELAY page");
		configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Show REVERB page");
		configParam(SHIMMER_PARAM, 0.f, 1.f, 0.f, "Shimmer (reverb only, always live)");
		configParam(SYNC_PARAM, 0.f, 1.f, 0.f, "Sync current page's TIME to clock divisions");
		// Right-click Randomize leaves the momentary buttons alone (their
		// real state lives inside the module); the FX knobs stay randomizable.
		for (int id : {DELAY_PARAM, REVERB_PARAM, SHIMMER_PARAM, SYNC_PARAM,
		               RATEDIV1_PARAM, DEPTH1_PARAM, RATEDIV2_PARAM, DEPTH2_PARAM,
		               RATEDIV3_PARAM, DEPTH3_PARAM, RATEDIV4_PARAM, DEPTH4_PARAM})
			paramQuantities[id]->randomizeEnabled = false;
		configParam(MIX_PARAM, 0.f, 1.f, 0.35f, "Mix (current page)");
		configParam(TIME_PARAM, 0.f, 1.f, 0.3f, "Time (current page)");
		configParam(REGEN_PARAM, 0.f, 1.f, 0.4f, "Regen -- feedback (delay) / decay (reverb)");
		configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone -- damping");
		configParam(SPEED_PARAM, 0.05f, 4.f, 1.f, "LFO global speed", " Hz");
		configInput(EXT_L_INPUT, "External L (also feeds metering when Command isn't adjacent)");
		configInput(EXT_R_INPUT, "External R");
		configOutput(MASTER_L_OUTPUT, "FX master L");
		configOutput(MASTER_R_OUTPUT, "FX master R");
		configOutput(TRIG1_OUTPUT, "Trigger 1 (also modulates Command's RATE when adjacent, hidden)");
		configOutput(TRIG2_OUTPUT, "Trigger 2 (also modulates Command's DENSITY when adjacent, hidden)");
		configOutput(TRIG3_OUTPUT, "Trigger 3 (also modulates Command's SWING when adjacent, hidden)");
		configOutput(TRIG4_OUTPUT, "Trigger 4 (also modulates Command's ENTROPY when adjacent, hidden)");

		leftExpander.producerMessage = &leftProducer;
		leftExpander.consumerMessage = &leftConsumer;
		rightExpander.producerMessage = &rightProducer;
		rightExpander.consumerMessage = &rightConsumer;

		onSampleRateChange();
	}

	// Right-click "Initialize": reset the module-side state too (RATE/DEPTH
	// levels, FX pages, shimmer/sync) and wipe every FX buffer, so Initialize
	// also clears any runaway delay/reverb.
	void onReset() override {
		for (int i = 0; i < 4; i++) lfo[i] = LfoChannel();
		delayPage = FxPage();
		reverbPage = FxPage();
		delayPageSelected = true;
		shimmerOn = false;
		delaySync = reverbSync = false;
		onSampleRateChange();
		meterMs = 0.f;
	}

	void onSampleRateChange() override {
		sr = APP->engine->getSampleRate();
		delayBufL.assign((int)(sr * 2.0), 0.f);
		delayBufR.assign((int)(sr * 2.0), 0.f);
		delayWriteIdx = 0;
		// Freeverb's canonical tuning ratios, scaled to our sample rate
		// instead of hardcoded for 44.1kHz.
		static const float combTunings[4] = {1116.f, 1188.f, 1277.f, 1356.f};
		static const float apTunings[2] = {556.f, 441.f};
		float scale = sr / 44100.f;
		for (int i = 0; i < 4; i++) {
			combL[i].setDelaySamples((int)(combTunings[i] * scale));
			combR[i].setDelaySamples((int)((combTunings[i] + 23.f) * scale)); // slight offset for stereo width
		}
		for (int i = 0; i < 2; i++) {
			apL[i].setDelaySamples((int)(apTunings[i] * scale));
			apR[i].setDelaySamples((int)((apTunings[i] + 17.f) * scale));
		}
		shimmerL.init((int)sr);
		shimmerR.init((int)sr);
	}

	static bool isCommand(Module* m) {
		return m && m->model && m->model->plugin && m->model->plugin->slug == "SpacesCommand"
			&& m->model->slug == "SpacesCommand";
	}
	// Stellar has nothing to send Intel and nothing is read from it here --
	// this exists purely so the LINK light recognizes any family member,
	// not just Command, matching the same "any neighbor lights it" rule
	// Command and Stellar apply for each other.
	static bool isStellarModule(Module* m) {
		return m && m->model && m->model->plugin && m->model->plugin->slug == "Stellar"
			&& m->model->slug == "Stellar";
	}
	static bool isBeams(Module* m) {
		return m && m->model && m->model->plugin && m->model->plugin->slug == "Beams"
			&& m->model->slug == "Beams";
	}

	void process(const ProcessArgs& args) override {
		if (firstProcess) { onSampleRateChange(); firstProcess = false; }

		// --- Page select (DELAY/REVERB) with instant knob snap ---
		bool pageChanged = false;
		if (delayPageBtn.process(params[DELAY_PARAM].getValue())) { delayPageSelected = true; pageChanged = true; }
		if (reverbPageBtn.process(params[REVERB_PARAM].getValue())) { delayPageSelected = false; pageChanged = true; }
		if (pageChanged) {
			FxPage& p = delayPageSelected ? delayPage : reverbPage;
			params[MIX_PARAM].setValue(p.mix);
			params[TIME_PARAM].setValue(p.time);
			params[REGEN_PARAM].setValue(p.regen);
			params[TONE_PARAM].setValue(p.tone);
		}
		// Continuously write the knobs' live values back into whichever
		// page is currently selected, so switching away and back preserves
		// them (this is the "stored value" half of the paging model).
		{
			FxPage& p = delayPageSelected ? delayPage : reverbPage;
			p.mix = params[MIX_PARAM].getValue();
			p.time = params[TIME_PARAM].getValue();
			p.regen = params[REGEN_PARAM].getValue();
			p.tone = params[TONE_PARAM].getValue();
		}
		if (shimmerBtn.process(params[SHIMMER_PARAM].getValue())) shimmerOn = !shimmerOn;
		if (syncBtnTrig.process(params[SYNC_PARAM].getValue())) {
			if (delayPageSelected) delaySync = !delaySync; else reverbSync = !reverbSync;
		}
		lights[DELAY_LIGHT].setBrightness(delayPageSelected ? 1.f : 0.2f);
		lights[REVERB_LIGHT].setBrightness(!delayPageSelected ? 1.f : 0.2f);
		lights[SHIMMER_LIGHT].setBrightness(shimmerOn ? 1.f : 0.f);
		lights[SYNC_LIGHT].setBrightness((delayPageSelected ? delaySync : reverbSync) ? 1.f : 0.f);

		// --- FX: delay always into reverb, both always live ---
		// A single non-finite sample (NaN/Inf from an upstream module) would
		// otherwise get written into the feedback buffers and circulate
		// forever -- a latched "full blast" that only reloading cures. Replace
		// it with silence, and cap the input at +/-20V.
		auto sane = [](float x) { return std::isfinite(x) ? clamp(x, -4.f, 4.f) : 0.f; };
		float dryL = sane(inputs[EXT_L_INPUT].getVoltage() / 5.f);
		float dryR = inputs[EXT_R_INPUT].isConnected() ? sane(inputs[EXT_R_INPUT].getVoltage() / 5.f) : dryL;

		float delayTimeSec = 0.02f + delayPage.time * 1.18f; // 20ms..1.2s
		int delaySamples = clamp((int)(delayTimeSec * sr), 1, (int)delayBufL.size() - 1);
		int readIdxL = (delayWriteIdx - delaySamples + (int)delayBufL.size()) % (int)delayBufL.size();
		int readIdxR = (delayWriteIdx - delaySamples + (int)delayBufR.size()) % (int)delayBufR.size();
		float delayWetL = delayBufL[readIdxL];
		float delayWetR = delayBufR[readIdxR];
		float delayToneCoeff = 0.15f + delayPage.tone * 0.7f;
		float fbL = delayToneL.process(delayWetL, delayToneCoeff);
		float fbR = delayToneR.process(delayWetR, delayToneCoeff);
		delayBufL[delayWriteIdx] = dryL + fbL * (delayPage.regen * 0.92f);
		delayBufR[delayWriteIdx] = dryR + fbR * (delayPage.regen * 0.92f);
		delayWriteIdx = (delayWriteIdx + 1) % (int)delayBufL.size();
		// (v6) Level fix: the old chain ADDED unity-gain wet on top of the full
		// dry signal, and a sustained tone through a feedback loop builds up
		// by 1/(1-feedback) -- with Stellar's hot output that pushed peaks to
		// 13V at default settings and 20-36V at higher REGEN (clipping the
		// Audio module). Now MIX crossfades (dry eases back to 50% at full
		// MIX) and the wet is scaled by sqrt(1-fb^2) so the sustained level
		// stays about that of the input at any REGEN.
		float delayFb = delayPage.regen * 0.92f;
		float delayComp = std::sqrt(std::max(0.05f, 1.f - delayFb * delayFb));
		float afterDelayL = dryL * (1.f - 0.5f * delayPage.mix) + delayWetL * delayPage.mix * delayComp;
		float afterDelayR = dryR * (1.f - 0.5f * delayPage.mix) + delayWetR * delayPage.mix * delayComp;

		// Reverb (Freeverb-style comb+allpass), fed by delay's output per
		// the confirmed delay->reverb series path. Shimmer taps the
		// reverb's own wet signal, pitch-shifts it up an octave, and
		// feeds it back into the reverb input -- a persistent toggle on
		// the always-live reverb engine, independent of which FX page
		// happens to be selected on the knobs right now.
		float reverbFeedback = 0.28f + reverbPage.regen * 0.7f;
		float reverbDamp = 0.1f + reverbPage.tone * 0.6f;
		float shimFeedL = shimmerOn ? shimmerL.process(afterDelayL) * 0.5f : 0.f;
		float shimFeedR = shimmerOn ? shimmerR.process(afterDelayR) * 0.5f : 0.f;
		float revInL = afterDelayL + shimFeedL;
		float revInR = afterDelayR + shimFeedR;
		float wetL = 0.f, wetR = 0.f;
		for (int i = 0; i < 4; i++) {
			wetL += combL[i].process(revInL, reverbFeedback, reverbDamp);
			wetR += combR[i].process(revInR, reverbFeedback, reverbDamp);
		}
		wetL *= 0.25f; wetR *= 0.25f;
		for (int i = 0; i < 2; i++) {
			wetL = apL[i].process(wetL, 0.5f);
			wetR = apR[i].process(wetR, 0.5f);
		}
		float reverbComp = std::sqrt(std::max(0.05f, 1.f - reverbFeedback * reverbFeedback));
		float outL = afterDelayL * (1.f - 0.5f * reverbPage.mix) + wetL * reverbPage.mix * reverbComp;
		float outR = afterDelayR * (1.f - 0.5f * reverbPage.mix) + wetR * reverbPage.mix * reverbComp;
		if (!std::isfinite(outL) || !std::isfinite(outR)) {
			// something blew up inside the FX: wipe every buffer instead of
			// letting NaN circulate, and output silence this sample
			onSampleRateChange();
			meterMs = 0.f;
			outL = outR = 0.f;
		}
		// Output limiter: transparent below 7V, then a smooth knee up to a hard
		// ceiling of 10V (Rack's clip level). It used to be clamp(+/-10)*5,
		// i.e. up to +/-50V -- far past what any audio output can take.
		auto softLimitV = [](float v) {
			float a = std::fabs(v);
			if (a <= 7.f) return v;
			float o = 7.f + 3.f * std::tanh((a - 7.f) / 3.f);
			return v < 0.f ? -o : o;
		};
		float vOutL = softLimitV(outL * 5.f), vOutR = softLimitV(outR * 5.f);
		outputs[MASTER_L_OUTPUT].setVoltage(vOutL);
		outputs[MASTER_R_OUTPUT].setVoltage(vOutR);

		// --- Link discovery (who's connected, for LINK lights + data messages) ---
		// cmdLeft/cmdRight = Command anywhere along the contiguous row of
		// modules on that side (other modules may sit in between);
		// adjLeft/adjRight = strictly adjacent, used only for LINK lights.
		Module* cmdLeft = intelLinkFind(this, false, "SpacesCommand");
		Module* cmdRight = intelLinkFind(this, true, "SpacesCommand");
		Module* beamsLeft = intelLinkFind(this, false, "Beams");
		Module* beamsRight = intelLinkFind(this, true, "Beams");
		bool adjLeft = isCommand(leftExpander.module);
		bool adjRight = isCommand(rightExpander.module);
		// --- VU meter: ALWAYS the level of Intel's own master output
		// (post delay/reverb), never LFO activity (an earlier version
		// switched to LFO trigger-pulse activity whenever Command was
		// anywhere in the connected row, which sat flat while audio played).
		// Scale matches Rack's own Audio module: 0 dB = +/-10V peak (its
		// clip level), so Rack's nominal +/-5V signals read -6 dB peak
		// instead of pegging the top. (The first version used 5V as 0 dB
		// with a peak detector, ~6dB hotter than the Audio module, so
		// ordinary signals sat at full.)
		// The BAR is a true VU: mean-square average (~100ms), which sits
		// well below the peaks like a real VU does. Maps -42..0 dB ->
		// empty..full. (No peak tick: removed, the dark marker read as a
		// stray trailing line at the end of the bar.)
		float mL = vOutL * 0.1f, mR = vOutR * 0.1f;  // what actually leaves the jacks: 1.0 = 10V
		float ms = 0.5f * (mL * mL + mR * mR);
		meterMs += (ms - meterMs) * std::min(1.f, args.sampleTime / 0.1f);
		if ((meterDiv++ & 31) == 0) {  // dB conversion is comparatively costly; ~1.5kHz update is plenty
			float rmsDb = 10.f * std::log10(std::max(meterMs, 1e-12f));
			lights[METER_LIGHT].setBrightness(clamp((rmsDb + 42.f) / 42.f, 0.f, 1.f));
		}

		// --- LFO / trigger channels ---
		static const int rateDivParams[4] = {RATEDIV1_PARAM, RATEDIV2_PARAM, RATEDIV3_PARAM, RATEDIV4_PARAM};
		static const int depthParams[4] = {DEPTH1_PARAM, DEPTH2_PARAM, DEPTH3_PARAM, DEPTH4_PARAM};
		static const int trigOutputs[4] = {TRIG1_OUTPUT, TRIG2_OUTPUT, TRIG3_OUTPUT, TRIG4_OUTPUT};
		static const int rateDivLights[4] = {RATEDIV1_LIGHT, RATEDIV2_LIGHT, RATEDIV3_LIGHT, RATEDIV4_LIGHT};
		static const int depthLights[4] = {DEPTH1_LIGHT, DEPTH2_LIGHT, DEPTH3_LIGHT, DEPTH4_LIGHT};
		float globalSpeedHz = params[SPEED_PARAM].getValue();
		// (v6) Beams' master clock: Beams bumps a tick counter in the shared
		// link message every tick (its CLOCK input, else its TEMPO). When
		// that's arriving, our triggers are DIVISIONS of it, so they land
		// exactly on Beams' grid; otherwise they free-run off SPEED as before.
		bool tickDriven = false;
		int ticksNow = 0;
		{
			const IntelModMessage* bm = nullptr;
			if (beamsLeft) bm = (const IntelModMessage*)leftExpander.consumerMessage;
			else if (beamsRight) bm = (const IntelModMessage*)rightExpander.consumerMessage;
			if (bm && bm->beamsTickValid) {
				tickDriven = true;
				if (!beamsTickSeen) { beamsTickSeen = true; lastBeamsTick = bm->beamsTick; }
				unsigned int d = bm->beamsTick - lastBeamsTick;
				lastBeamsTick = bm->beamsTick;
				ticksNow = (d > 8u) ? 1 : (int)d;   // a big jump = we just (re)linked: count it once
			}
			else {
				beamsTickSeen = false;
			}
		}
		for (int i = 0; i < 4; i++) {
			lfo[i].handleButtons(params[rateDivParams[i]].getValue(), params[depthParams[i]].getValue());
			lfo[i].step(sr, globalSpeedHz, tickDriven, ticksNow);
			outputs[trigOutputs[i]].setVoltage(lfo[i].trigVoltage(args.sampleTime));
			// Fill-bar convention: both buttons are 4-state (0, 1/3, 2/3, full),
			// so a single light's brightness directly IS the fill fraction
			// the widget draws -- no color-coding needed, the widget's own
			// fixed accent color (rate-div vs depth) carries identity.
			lights[rateDivLights[i]].setBrightness(lfo[i].rateDivState / 3.f);  // (v6) 0 -> empty gauge, same as DEPTH
			// (v5) DEPTH's fill now genuinely spans 0 (no LFO) -> 1 (max
			// LFO) in 3 even steps, matching depthState 0..3 exactly --
			// was (state+1)/4, which never showed empty even at state 0.
			lights[depthLights[i]].setBrightness(lfo[i].depthState / 3.f);
		}

		// --- Invisible Command link: send modulation offsets, floor-protected ---
		// Correct expander idiom (verified against how this project's own
		// LINK-light adjacency check already works, extended to actual
		// data): to send TO a neighbor, you write into THAT NEIGHBOR's own
		// producerMessage buffer (which it allocated for itself) and flag
		// ITS expander for flip -- not your own. The neighbor then reads
		// its own consumerMessage on its next process() call. Writing into
		// your own producerMessage/flipping your own expander (what I had
		// here initially) would just make you read your own data back.
		lights[LINK_LEFT_LIGHT].setBrightness(adjLeft || isStellarModule(leftExpander.module) || isBeams(leftExpander.module) ? 1.f : 0.f);
		lights[LINK_RIGHT_LIGHT].setBrightness(adjRight || isStellarModule(rightExpander.module) || isBeams(rightExpander.module) ? 1.f : 0.f);
		if (beamsLeft || beamsRight) {
			// Beams link: a single DECAY offset, sourced from the REVERB
			// page's stored tone value specifically (not just "whatever
			// params[TONE_PARAM] currently shows"), so Beams' decay
			// character doesn't jump around just because the user pages
			// Intel over to DELAY to tweak something unrelated.
			IntelModMessage bmsg;
			bmsg.decayOffset = reverbPage.tone;
			bmsg.present = true;
			if (beamsLeft) {
				Module* bm = beamsLeft;
				*(IntelModMessage*)bm->rightExpander.producerMessage = bmsg;
				bm->rightExpander.messageFlipRequested = true;
			}
			if (beamsRight) {
				Module* bm = beamsRight;
				*(IntelModMessage*)bm->leftExpander.producerMessage = bmsg;
				bm->leftExpander.messageFlipRequested = true;
			}
		}
		if (cmdLeft || cmdRight) {
			IntelModMessage msg;
			msg.rateOffset = lfo[0].modValue;
			msg.densOffset = lfo[1].modValue;
			msg.swingOffset = lfo[2].modValue;
			msg.entropyOffset = lfo[3].modValue;
			// (v5) static DEPTH-setting level per channel, for Command's
			// new depth-gauge display -- same 0..1-in-3-steps scale as
			// the button's own fill (see brightness calc above).
			msg.rateDepth = lfo[0].depthState / 3.f;
			msg.densDepth = lfo[1].depthState / 3.f;
			msg.swingDepth = lfo[2].depthState / 3.f;
			msg.entropyDepth = lfo[3].depthState / 3.f;
			msg.present = true;
			// Command applies its own floor protection against these
			// offsets (it knows its real parameter ranges); Intel just
			// sends a bounded -1..1-ish value either way.
			if (cmdLeft) {
				Module* cmd = cmdLeft;
				*(IntelModMessage*)cmd->rightExpander.producerMessage = msg;
				cmd->rightExpander.messageFlipRequested = true;
			}
			if (cmdRight) {
				Module* cmd = cmdRight;
				*(IntelModMessage*)cmd->leftExpander.producerMessage = msg;
				cmd->leftExpander.messageFlipRequested = true;
			}
		}
	}
};

// ---------------------------------------------------------------------
// Widget-side buttons: hand-drawn, matching Command's own proven
// SquareButton pattern (self-contained ParamWidget reading its own
// module's light(s) directly in draw()) rather than betting on an exact
// core-library combined light+button class name that couldn't be
// compile-checked against the real SDK this session.
// ---------------------------------------------------------------------

// Octagon on/off button (DELAY/REVERB page-select, SHIMMER, SYNC) -- each
// instance gets its own identity color (not one shared color for all
// four); unlit = outline only, lit = filled, matching the family's
// empty-until-lit button convention.
struct IntelButton : ParamWidget {
	Module* mod = nullptr;
	int lightId = -1;
	NVGcolor color = nvgRGB(0x8A, 0x64, 0x23);

	IntelButton() { box.size = mm2px(Vec(4.6, 4.6)); }

	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (ParamQuantity* pq = getParamQuantity()) pq->setValue(1.f);
			e.consume(this);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		ParamWidget::onDragEnd(e);
		if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
	}
	void draw(const DrawArgs& args) override {
		bool lit = mod && lightId >= 0 && mod->lights[lightId].getBrightness() > 0.5f;
		float r = box.size.x / 2.f, cx = r, cy = box.size.y / 2.f;
		nvgBeginPath(args.vg);
		for (int i = 0; i < 8; i++) {
			float a = (22.5f + 45.f * i) * (float)M_PI / 180.f;
			float x = cx + r * std::cos(a), y = cy + r * std::sin(a);
			if (i == 0) nvgMoveTo(args.vg, x, y); else nvgLineTo(args.vg, x, y);
		}
		nvgClosePath(args.vg);
		if (lit) {
			nvgFillColor(args.vg, color);
			nvgFill(args.vg);
		}
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.5, 0)).x);
		nvgStroke(args.vg);
	}
};

// Vertical 4-level fill-bar button (TRIG's rate-div/depth cycle controls).
// Both buttons on a channel are 4-state, shown as 1/4..4/4 fill from the
// bottom, rather than a flat color per state -- state is read straight
// off a single light's brightness (already stored as the fill fraction
// by the DSP side), and each instance carries its own fixed accent color
// (rate-div vs depth) so identity doesn't depend on the fill level.
struct IntelBarButton : ParamWidget {
	Module* mod = nullptr;
	int lightId = -1;
	NVGcolor color = nvgRGB(0x7F, 0x77, 0xDD);

	IntelBarButton() { box.size = mm2px(Vec(4.6, 4.6)); }

	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (ParamQuantity* pq = getParamQuantity()) pq->setValue(1.f);
			e.consume(this);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		ParamWidget::onDragEnd(e);
		if (ParamQuantity* pq = getParamQuantity()) pq->setValue(0.f);
	}
	void draw(const DrawArgs& args) override {
		float level = (mod && lightId >= 0) ? mod->lights[lightId].getBrightness() : 0.25f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, mm2px(Vec(0.5, 0)).x);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, mm2px(Vec(0.45, 0)).x);
		nvgStroke(args.vg);
		float inset = mm2px(Vec(0.5, 0)).x;
		float fullH = box.size.y - 2.f * inset;
		float fillH = fullH * clamp(level, 0.f, 1.f);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, inset, box.size.y - inset - fillH, box.size.x - 2.f * inset, fillH);
		nvgFillColor(args.vg, color);
		nvgFill(args.vg);
	}
};

// Full-height gradient bar meter, replacing a discrete LED ladder --
// static groove is in the SVG, this widget draws the dynamic fill on top,
// green->amber->red by level, matching the "nice bar-based visual, full
// section height" request.
struct IntelMeterWidget : Widget {
	Module* mod = nullptr;
	int lightId = -1;

	void draw(const DrawArgs& args) override {
		float level = (mod && lightId >= 0) ? mod->lights[lightId].getBrightness() : 0.f;
		level = clamp(level, 0.f, 1.f);
		float fillW = box.size.x * level;
		if (fillW < 1.f) return;
		NVGpaint grad = nvgLinearGradient(args.vg, 0.f, 0.f, box.size.x, 0.f,
			nvgRGB(0x4C, 0xA6, 0x4C), nvgRGB(0xD1, 0x4A, 0x3A));
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, fillW, box.size.y, mm2px(Vec(1.0, 0)).x);
		nvgFillPaint(args.vg, grad);
		nvgFill(args.vg);
	}
};

// FX knob -- v3 tried to shrink the stock RoundBlackKnob just by setting
// box.size, which does nothing visually: RoundBlackKnob draws a fixed-
// size SVG asset regardless of box.size (the exact issue Command already
// hit and documented for its own FEEL knobs, see SmallKnob85 there), so
// the FX knobs had actually been rendering at full stock size (~9.6mm)
// the whole time. This wraps RoundBlackKnob with a real nvgScale at draw
// time, same technique as Command's SmallKnob85, so the rendered knob is
// genuinely 6.0mm -- slightly bigger than Stellar's own 5.6mm StellarKnob,
// per direct request -- while box.size (hit-testing) is scaled to match.
struct IntelKnob : RoundBlackKnob {
	static constexpr float SCALE = 0.625f; // 6.0mm / RoundBlackKnob's 9.6mm native size
	IntelKnob() {
		box.size = box.size.mult(SCALE);
	}
	void draw(const DrawArgs& args) override {
		nvgSave(args.vg);
		nvgTranslate(args.vg, box.size.x / 2.f, box.size.y / 2.f);
		nvgScale(args.vg, SCALE, SCALE);
		nvgTranslate(args.vg, -box.size.x / 2.f / SCALE, -box.size.y / 2.f / SCALE);
		RoundBlackKnob::draw(args);
		nvgRestore(args.vg);
	}
};

// ---------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------

struct IntelWidget : ModuleWidget {
	IntelWidget(Intel* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Intel.svg")));
		// Deliberately NOT setting box.size manually here -- that exact
		// mistake (mm2px() rounding off RACK_GRID_HEIGHT by a fraction of
		// a pixel) crashed Stellar on placement earlier in this project.
		// setPanel() auto-sizes from the SVG's own declared height, which
		// is the only path that lands exactly on 380px.

		addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(5.5, 5.5)), module, Intel::LINK_LEFT_LIGHT));
		addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(65.62, 5.5)), module, Intel::LINK_RIGHT_LIGHT));

		// I/O row -- coordinates below are copied directly from
		// intel_layout.json (generated by intel_layout.py alongside the
		// SVG from the same numbers), not re-derived by hand, so the
		// widgets can't drift out of sync with the panel artwork the way
		// Stellar's once did.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.4, 37.51)), module, Intel::EXT_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.51, 37.51)), module, Intel::EXT_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(45.61, 37.51)), module, Intel::MASTER_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.72, 37.51)), module, Intel::MASTER_R_OUTPUT));

		// TRIG sub-panels: jack, then rate-div/depth fill-bar buttons stacked vertically
		static const float jackX[4] = {16.64f, 30.92f, 45.2f, 59.48f};
		static const int trigOut[4] = {Intel::TRIG1_OUTPUT, Intel::TRIG2_OUTPUT, Intel::TRIG3_OUTPUT, Intel::TRIG4_OUTPUT};
		static const int rateDivParam[4] = {Intel::RATEDIV1_PARAM, Intel::RATEDIV2_PARAM, Intel::RATEDIV3_PARAM, Intel::RATEDIV4_PARAM};
		static const int depthParam[4] = {Intel::DEPTH1_PARAM, Intel::DEPTH2_PARAM, Intel::DEPTH3_PARAM, Intel::DEPTH4_PARAM};
		static const int rateDivLight[4] = {Intel::RATEDIV1_LIGHT, Intel::RATEDIV2_LIGHT, Intel::RATEDIV3_LIGHT, Intel::RATEDIV4_LIGHT};
		static const int depthLight[4] = {Intel::DEPTH1_LIGHT, Intel::DEPTH2_LIGHT, Intel::DEPTH3_LIGHT, Intel::DEPTH4_LIGHT};
		for (int i = 0; i < 4; i++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jackX[i], 57.86)), module, trigOut[i]));
			auto* rd = createParamCentered<IntelBarButton>(mm2px(Vec(jackX[i], 74.31)), module, rateDivParam[i]);
			rd->mod = module; rd->lightId = rateDivLight[i]; rd->color = nvgRGB(0x4A, 0x6F, 0xA5); // steel blue (was purple)
			addParam(rd);
			auto* dp = createParamCentered<IntelBarButton>(mm2px(Vec(jackX[i], 88.35)), module, depthParam[i]);
			dp->mod = module; dp->lightId = depthLight[i]; dp->color = nvgRGB(0x1D, 0x7A, 0x5C);
			addParam(dp);
		}

		// FX buttons -- octagons, each with its own identity color
		static const float fxBtnX[4] = {13.5f, 29.87f, 46.25f, 62.62f};
		static const int fxBtnParam[4] = {Intel::DELAY_PARAM, Intel::REVERB_PARAM, Intel::SHIMMER_PARAM, Intel::SYNC_PARAM};
		static const int fxBtnLight[4] = {Intel::DELAY_LIGHT, Intel::REVERB_LIGHT, Intel::SHIMMER_LIGHT, Intel::SYNC_LIGHT};
		static const NVGcolor fxBtnColor[4] = {nvgRGB(0x8A, 0x64, 0x23), nvgRGB(0x8A, 0x2A, 0x2A), nvgRGB(0x2E, 0x4A, 0x6E), nvgRGB(0x6B, 0x4C, 0x8A)};
		for (int i = 0; i < 4; i++) {
			auto* b = createParamCentered<IntelButton>(mm2px(Vec(fxBtnX[i], 105.1)), module, fxBtnParam[i]);
			b->mod = module; b->lightId = fxBtnLight[i]; b->color = fxBtnColor[i];
			addParam(b);
		}

		// FX knobs -- see IntelKnob above: genuinely 6.0mm now, not just hit-box-scaled
		static const float fxKnobX[5] = {13.5f, 25.78f, 38.06f, 50.34f, 62.62f};
		static const int fxKnobParam[5] = {Intel::MIX_PARAM, Intel::TIME_PARAM, Intel::REGEN_PARAM, Intel::TONE_PARAM, Intel::SPEED_PARAM};
		for (int i = 0; i < 5; i++) {
			addParam(createParamCentered<IntelKnob>(mm2px(Vec(fxKnobX[i], 115.8)), module, fxKnobParam[i]));
		}

		// Meter: single full-height gradient bar, position/size from intel_layout.json
		auto* meter = new IntelMeterWidget();
		meter->box.pos = mm2px(Vec(10.9, 13.9));
		meter->box.size = mm2px(Vec(54.32, 11.41));
		meter->mod = module;
		meter->lightId = Intel::METER_LIGHT;
		addChild(meter);
	}
};

Model* modelIntel = createModel<Intel, IntelWidget>("Intel");
