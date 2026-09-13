#include "plugin.hpp"
#include <cmath>

// ---------------------------------------------------------------------
// Stellar -- dual-voice companion synth to Spaces Command.
// Fully standalone: takes plain V/OCT+GATE+MOD per voice from anything,
// no VCV Expander adjacency required. The LINK lights are a pure bonus
// indicator (lit when a Spaces Command sits directly adjacent, on
// either side) -- no cross-module data exchange is implemented yet,
// since Command itself doesn't produce any expander message today.
// That deeper integration (MORPH mirroring, A/B VEL pass-through) was
// discussed but deliberately deferred; this build only does adjacency
// detection for the light, nothing else depends on it.
//
// Per voice: 4 switchable oscillators (AN/FM/SS/PL, independent
// booleans, mixed with equal-power normalization -- same convention as
// the original engine) -> ONE shared Moog-style 4-pole ladder filter
// (CUTOFF only; the old multi-purpose TIMBRE idea was dropped, so PL's
// pulse width is now a fixed 50% square rather than knob-swept -- a
// disclosed simplification) with a fixed internal bass-compensation
// curve instead of an exposed RESONANCE knob -> ADSR VCA -> external
// MOD input as a second VCA stage (unpatched = fully open, so plain
// ADSR-only behavior is unchanged if MOD is never touched).
//
// SUB1/SUB2: simple square sub-oscillators tracking that voice's own
// V/OCT, gated directly by that voice's raw GATE (on/off, not shaped by
// the ADSR) -- -1 octave for Voice 1, -2 octaves for Voice 2, each its
// own dedicated output, undisclosed further shaping left to whatever
// they're patched into.
//
// NOISE: a standalone raw noise generator, always running, untouched by
// either voice's envelope or filter -- its own output, not
// automatically summed into the master mix.
//
// EXT-IN: one stereo-capable input. A mono cable normals to both
// Master L and Master R equally.
// ---------------------------------------------------------------------

struct Stellar : Module {
	enum ParamId {
		AN1_PARAM, FM1_PARAM, SS1_PARAM, PL1_PARAM,
		ATTACK1_PARAM, DECAY1_PARAM, SUSTAIN1_PARAM, RELEASE1_PARAM, CUTOFF1_PARAM,
		AN2_PARAM, FM2_PARAM, SS2_PARAM, PL2_PARAM,
		ATTACK2_PARAM, DECAY2_PARAM, SUSTAIN2_PARAM, RELEASE2_PARAM, CUTOFF2_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT1_INPUT, GATE1_INPUT, MOD1_INPUT,
		VOCT2_INPUT, GATE2_INPUT, MOD2_INPUT,
		EXTIN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		SUB1_OUTPUT, SUB2_OUTPUT, NOISE_OUTPUT, MASTER_L_OUTPUT, MASTER_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		AN1_LIGHT, FM1_LIGHT, SS1_LIGHT, PL1_LIGHT,
		AN2_LIGHT, FM2_LIGHT, SS2_LIGHT, PL2_LIGHT,
		LINK_LEFT_LIGHT, LINK_RIGHT_LIGHT,
		LIGHTS_LEN
	};

	// --- Per-voice oscillator/filter/envelope state ---
	struct VoiceEngine {
		float phase = 0.f;
		float phaseInc = 0.f;
		float fmModPhase = 0.f;
		float sawPhases[7] = {};
		float sawPhaseInc[7] = {};

		// 4-pole Moog-style ladder filter state (one pole per stage)
		float lp[4] = {};

		enum class Env { Idle, Attack, Decay, Sustain, Release };
		Env envState = Env::Idle;
		float envVal = 0.f;
		float releaseLevel = 0.f;
		double stateTime = 0.0;
		int activeNoteCount = 0;

		// Raw on/off sub-oscillator, independent of the ADSR above
		float subPhase = 0.f;

		void triggerNote(float freqHz, float sampleRate) {
			phaseInc = freqHz / sampleRate;
			static const float detunes[7] = {-0.06f,-0.04f,-0.015f,0.f,0.015f,0.04f,0.06f};
			for (int i = 0; i < 7; i++) {
				float df = freqHz * std::pow(2.f, detunes[i] / 12.f);
				sawPhaseInc[i] = df / sampleRate;
			}
			if (activeNoteCount == 0) {
				envState = Env::Attack;
				stateTime = 0.0;
				envVal = 0.f;
			}
			activeNoteCount++;
		}

		void releaseNote() {
			if (activeNoteCount > 0) activeNoteCount--;
			if (activeNoteCount == 0 && envState != Env::Idle && envState != Env::Release) {
				envState = Env::Release;
				releaseLevel = envVal;
				stateTime = 0.0;
			}
		}

		// Matches the original engine's linear ADSR segments exactly.
		void stepEnvelope(float sampleRate, float attack, float decay, float sustain, float release) {
			double dt = 1.0 / sampleRate;
			stateTime += dt;
			switch (envState) {
				case Env::Idle: envVal = 0.f; break;
				case Env::Attack: {
					float dur = std::max(0.001f, attack);
					envVal = (float)(stateTime / dur);
					if (envVal >= 1.f) { envVal = 1.f; envState = Env::Decay; stateTime = 0.0; }
					break;
				}
				case Env::Decay: {
					float dur = std::max(0.001f, decay);
					float prog = (float)(stateTime / dur);
					if (prog >= 1.f) { envVal = sustain; envState = Env::Sustain; stateTime = 0.0; }
					else envVal = 1.f - (1.f - sustain) * prog;
					break;
				}
				case Env::Sustain: envVal = sustain; break;
				case Env::Release: {
					float dur = std::max(0.001f, release);
					float prog = (float)(stateTime / dur);
					if (prog >= 1.f) { envVal = 0.f; envState = Env::Idle; }
					else envVal = releaseLevel * (1.f - prog);
					break;
				}
			}
		}

		// One shared 4-pole ladder filter for whichever oscillators are
		// active this sample, with a fixed internal bass-compensation
		// curve standing in for a manual resonance knob -- boosts low
		// end back in proportionally as cutoff rises, so the filter
		// doesn't thin out at high settings the way a bare one-pole
		// design would.
		float ladderFilter(float in, float cutoffNorm, float sampleRate) {
			float cutoffHz = 40.f + cutoffNorm * cutoffNorm * 9000.f;
			float wd = 2.f * (float)M_PI * cutoffHz / sampleRate;
			float g = std::tan(wd * 0.5f);
			float G = g / (1.f + g);
			// Fixed internal feedback -- not user-exposed -- gives the
			// ladder some real character without a self-oscillation
			// control to tune.
			float fb = 0.25f;
			float in1 = in - fb * lp[3];
			lp[0] += G * (in1 - lp[0]);
			lp[1] += G * (lp[0] - lp[1]);
			lp[2] += G * (lp[1] - lp[2]);
			lp[3] += G * (lp[2] - lp[3]);
			// Bass-compensation: blend a touch of the pre-filter signal's
			// low end (approximated by stage-2 output, already 2-pole
			// smoothed) back in, scaled up as cutoff rises.
			float bassComp = 0.18f * cutoffNorm * lp[1];
			return lp[3] + bassComp;
		}

		float processOscMix(bool an, bool fm, bool ss, bool pl, float cutoffNorm, float sampleRate) {
			float total = 0.f;
			int activeCount = 0;
			bool needsAdvance = an || fm || pl;

			if (an) {
				float wave = 2.f * phase - 1.f;
				total += wave; activeCount++;
			}
			if (fm) {
				float modInc = phaseInc * 3.5f;
				fmModPhase += modInc;
				if (fmModPhase >= 1.f) fmModPhase -= 1.f;
				float modOut = std::sin(fmModPhase * 2.f * (float)M_PI);
				float modIndex = 3.0f;  // fixed -- CUTOFF no longer doubles as FM index (dedicated filter knob now)
				float carrierPhase = phase + modOut * modIndex * phaseInc;
				float carrier = std::sin(carrierPhase * 2.f * (float)M_PI);
				total += carrier; activeCount++;
			}
			if (ss) {
				float sum = 0.f;
				for (int i = 0; i < 7; i++) {
					float w = 2.f * sawPhases[i] - 1.f;
					sum += w;
					sawPhases[i] += sawPhaseInc[i];
					if (sawPhases[i] >= 1.f) sawPhases[i] -= 1.f;
				}
				total += sum * 0.35f; activeCount++;
			}
			if (pl) {
				// Fixed 50% duty -- PWM sweep was tied to the old
				// multi-purpose TIMBRE knob, which no longer exists.
				float wave = (phase < 0.5f) ? 0.4f : -0.4f;
				total += wave; activeCount++;
			}
			if (needsAdvance) {
				phase += phaseInc;
				if (phase >= 1.f) phase -= 1.f;
			}
			if (activeCount > 1) total /= std::sqrt((float)activeCount);

			float filtered = (activeCount > 0) ? ladderFilter(total, cutoffNorm, sampleRate) : 0.f;
			return filtered * envVal;
		}

		float processSub(float freqHz, float sampleRate, bool gateOn) {
			subPhase += freqHz / sampleRate;
			if (subPhase >= 1.f) subPhase -= 1.f;
			float wave = (subPhase < 0.5f) ? 1.f : -1.f;
			return gateOn ? wave : 0.f;
		}
	};

	VoiceEngine v1, v2;
	uint32_t noiseState = 0x2f6e2b1;

	float noiseSample() {
		// Simple xorshift -- fast, deterministic-per-seed, no external deps.
		noiseState ^= noiseState << 13;
		noiseState ^= noiseState >> 17;
		noiseState ^= noiseState << 5;
		return ((int32_t)noiseState / 2147483648.f);
	}

	Stellar() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(AN1_PARAM, "Analog");
		configButton(FM1_PARAM, "FM");
		configButton(SS1_PARAM, "Supersaw");
		configButton(PL1_PARAM, "Pulse");
		configParam(ATTACK1_PARAM, 0.001f, 4.f, 0.01f, "Attack", " s");
		configParam(DECAY1_PARAM, 0.001f, 4.f, 0.35f, "Decay", " s");
		configParam(SUSTAIN1_PARAM, 0.f, 1.f, 0.70f, "Sustain", "%", 0, 100);
		configParam(RELEASE1_PARAM, 0.001f, 4.f, 0.25f, "Release", " s");
		configParam(CUTOFF1_PARAM, 0.f, 1.f, 0.5f, "Cutoff (Moog-style ladder, -24dB/oct, fixed internal bass compensation)");
		configButton(AN2_PARAM, "Analog");
		configButton(FM2_PARAM, "FM");
		configButton(SS2_PARAM, "Supersaw");
		configButton(PL2_PARAM, "Pulse");
		configParam(ATTACK2_PARAM, 0.001f, 4.f, 0.01f, "Attack", " s");
		configParam(DECAY2_PARAM, 0.001f, 4.f, 0.35f, "Decay", " s");
		configParam(SUSTAIN2_PARAM, 0.f, 1.f, 0.70f, "Sustain", "%", 0, 100);
		configParam(RELEASE2_PARAM, 0.001f, 4.f, 0.25f, "Release", " s");
		configParam(CUTOFF2_PARAM, 0.f, 1.f, 0.5f, "Cutoff (Moog-style ladder, -24dB/oct, fixed internal bass compensation)");

		configInput(VOCT1_INPUT, "Voice 1 pitch (1V/oct)");
		configInput(GATE1_INPUT, "Voice 1 gate");
		configInput(MOD1_INPUT, "Voice 1 MOD (VCA stage -- envelope x this CV; unpatched = fully open)");
		configInput(VOCT2_INPUT, "Voice 2 pitch (1V/oct)");
		configInput(GATE2_INPUT, "Voice 2 gate");
		configInput(MOD2_INPUT, "Voice 2 MOD (VCA stage -- envelope x this CV; unpatched = fully open)");
		configInput(EXTIN_INPUT, "External audio in (mono normals to both Master L and R)");

		configOutput(SUB1_OUTPUT, "Voice 1 sub-oscillator (-1 octave, gated raw on/off by GATE 1)");
		configOutput(SUB2_OUTPUT, "Voice 2 sub-oscillator (-2 octaves, gated raw on/off by GATE 2)");
		configOutput(NOISE_OUTPUT, "Noise (standalone, always-on, unshaped)");
		configOutput(MASTER_L_OUTPUT, "Master L");
		configOutput(MASTER_R_OUTPUT, "Master R");
	}

	dsp::SchmittTrigger an1Trig, fm1Trig, ss1Trig, pl1Trig;
	dsp::SchmittTrigger an2Trig, fm2Trig, ss2Trig, pl2Trig;
	bool an1On = false, fm1On = false, ss1On = false, pl1On = false;
	bool an2On = false, fm2On = false, ss2On = false, pl2On = false;
	dsp::SchmittTrigger gate1Trig, gate2Trig;
	bool gate1High = false, gate2High = false;

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "an1", json_boolean(an1On));
		json_object_set_new(rootJ, "fm1", json_boolean(fm1On));
		json_object_set_new(rootJ, "ss1", json_boolean(ss1On));
		json_object_set_new(rootJ, "pl1", json_boolean(pl1On));
		json_object_set_new(rootJ, "an2", json_boolean(an2On));
		json_object_set_new(rootJ, "fm2", json_boolean(fm2On));
		json_object_set_new(rootJ, "ss2", json_boolean(ss2On));
		json_object_set_new(rootJ, "pl2", json_boolean(pl2On));
		return rootJ;
	}
	void dataFromJson(json_t* rootJ) override {
		auto load = [&](const char* key, bool& target) {
			json_t* j = json_object_get(rootJ, key);
			if (j) target = json_boolean_value(j);
		};
		load("an1", an1On); load("fm1", fm1On); load("ss1", ss1On); load("pl1", pl1On);
		load("an2", an2On); load("fm2", fm2On); load("ss2", ss2On); load("pl2", pl2On);
	}

	void process(const ProcessArgs& args) override {
		if (an1Trig.process(params[AN1_PARAM].getValue())) an1On = !an1On;
		if (fm1Trig.process(params[FM1_PARAM].getValue())) fm1On = !fm1On;
		if (ss1Trig.process(params[SS1_PARAM].getValue())) ss1On = !ss1On;
		if (pl1Trig.process(params[PL1_PARAM].getValue())) pl1On = !pl1On;
		if (an2Trig.process(params[AN2_PARAM].getValue())) an2On = !an2On;
		if (fm2Trig.process(params[FM2_PARAM].getValue())) fm2On = !fm2On;
		if (ss2Trig.process(params[SS2_PARAM].getValue())) ss2On = !ss2On;
		if (pl2Trig.process(params[PL2_PARAM].getValue())) pl2On = !pl2On;
		lights[AN1_LIGHT].setBrightness(an1On ? 1.f : 0.f);
		lights[FM1_LIGHT].setBrightness(fm1On ? 1.f : 0.f);
		lights[SS1_LIGHT].setBrightness(ss1On ? 1.f : 0.f);
		lights[PL1_LIGHT].setBrightness(pl1On ? 1.f : 0.f);
		lights[AN2_LIGHT].setBrightness(an2On ? 1.f : 0.f);
		lights[FM2_LIGHT].setBrightness(fm2On ? 1.f : 0.f);
		lights[SS2_LIGHT].setBrightness(ss2On ? 1.f : 0.f);
		lights[PL2_LIGHT].setBrightness(pl2On ? 1.f : 0.f);

		// LINK lights: pure adjacency detection by plugin/model slug
		// strings, not a shared C++ symbol -- Stellar and Spaces Command
		// are now genuinely separate compiled plugins (different
		// binaries), so there's no modelSpacesCommand symbol to
		// reference directly the way there would be within one plugin.
		// No message exchange implemented -- Command doesn't produce an
		// expander message today, so this is intentionally just a
		// presence light.
		auto isSpacesCommand = [](Module* m) {
			return m && m->model && m->model->plugin && m->model->plugin->slug == "Spaces" && m->model->slug == "SpacesCommand";
		};
		bool linkedLeft = isSpacesCommand(leftExpander.module);
		bool linkedRight = isSpacesCommand(rightExpander.module);
		lights[LINK_LEFT_LIGHT].setBrightness(linkedLeft ? 1.f : 0.f);
		lights[LINK_RIGHT_LIGHT].setBrightness(linkedRight ? 1.f : 0.f);

		float sr = args.sampleRate;

		// --- Voice 1 ---
		bool g1 = inputs[GATE1_INPUT].getVoltage() >= 1.f;
		if (gate1Trig.process(g1 ? 10.f : 0.f)) v1.triggerNote(261.6256f * std::pow(2.f, inputs[VOCT1_INPUT].getVoltage()), sr);
		if (gate1High && !g1) v1.releaseNote();
		gate1High = g1;
		v1.stepEnvelope(sr, params[ATTACK1_PARAM].getValue(), params[DECAY1_PARAM].getValue(),
		                 params[SUSTAIN1_PARAM].getValue(), params[RELEASE1_PARAM].getValue());
		float v1osc = v1.processOscMix(an1On, fm1On, ss1On, pl1On, params[CUTOFF1_PARAM].getValue(), sr);
		float mod1 = inputs[MOD1_INPUT].isConnected() ? clamp(inputs[MOD1_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
		float voice1Out = v1osc * mod1 * 5.f;

		float sub1Freq = 261.6256f * std::pow(2.f, inputs[VOCT1_INPUT].getVoltage()) * 0.5f;  // -1 octave
		float sub1 = v1.processSub(sub1Freq, sr, g1) * 5.f;
		outputs[SUB1_OUTPUT].setVoltage(sub1);

		// --- Voice 2 ---
		bool g2 = inputs[GATE2_INPUT].getVoltage() >= 1.f;
		if (gate2Trig.process(g2 ? 10.f : 0.f)) v2.triggerNote(261.6256f * std::pow(2.f, inputs[VOCT2_INPUT].getVoltage()), sr);
		if (gate2High && !g2) v2.releaseNote();
		gate2High = g2;
		v2.stepEnvelope(sr, params[ATTACK2_PARAM].getValue(), params[DECAY2_PARAM].getValue(),
		                 params[SUSTAIN2_PARAM].getValue(), params[RELEASE2_PARAM].getValue());
		float v2osc = v2.processOscMix(an2On, fm2On, ss2On, pl2On, params[CUTOFF2_PARAM].getValue(), sr);
		float mod2 = inputs[MOD2_INPUT].isConnected() ? clamp(inputs[MOD2_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
		float voice2Out = v2osc * mod2 * 5.f;

		float sub2Freq = 261.6256f * std::pow(2.f, inputs[VOCT2_INPUT].getVoltage()) * 0.25f;  // -2 octaves
		float sub2 = v2.processSub(sub2Freq, sr, g2) * 5.f;
		outputs[SUB2_OUTPUT].setVoltage(sub2);

		// --- Noise: standalone, always-on, own output only ---
		outputs[NOISE_OUTPUT].setVoltage(noiseSample() * 5.f);

		// --- Master mix: both voices + EXT-IN (mono normals to both channels) ---
		float extIn = inputs[EXTIN_INPUT].getVoltage();
		float masterL = voice1Out + voice2Out + extIn;
		float masterR = voice1Out + voice2Out + extIn;
		outputs[MASTER_L_OUTPUT].setVoltage(masterL);
		outputs[MASTER_R_OUTPUT].setVoltage(masterR);
	}
};

// ---------------------------------------------------------------------
// Widgets -- reuses the same visual language as SpacesCommand (empty-
// until-lit SquareButton, same palette) for family consistency, per
// the earlier design decision that shared design language (not a
// repeated "Spaces" prefix) is what signals the modules are linked.
// ---------------------------------------------------------------------

struct StellarButton : ParamWidget {
	Module* mod = nullptr;
	int lightId = -1;
	NVGcolor litColor = nvgRGB(0xE0, 0x40, 0x40);
	NVGcolor unlitColor = nvgRGB(0xED, 0xE7, 0xDC);

	StellarButton() {
		box.size = mm2px(Vec(4.6, 4.6));
	}
	void onButton(const ButtonEvent& e) override {
		ParamWidget::onButton(e);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			ParamQuantity* pq = getParamQuantity();
			if (pq) pq->setValue(1.f);
			e.consume(this);
		}
	}
	void onDragEnd(const DragEndEvent& e) override {
		ParamWidget::onDragEnd(e);
		ParamQuantity* pq = getParamQuantity();
		if (pq) pq->setValue(0.f);
	}
	void draw(const DrawArgs& args) override {
		bool lit = (mod && lightId >= 0) ? mod->lights[lightId].getBrightness() > 0.5f : false;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0.7f);
		nvgFillColor(args.vg, lit ? litColor : unlitColor);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x1A, 0x18, 0x14));
		nvgStrokeWidth(args.vg, 0.9f);
		nvgStroke(args.vg);
	}
};

struct StellarKnob : Knob {
	StellarKnob() {
		box.size = mm2px(Vec(5.6, 5.6));
	}
};

struct StellarWidget : ModuleWidget {
	StellarWidget(Stellar* module) {
		setModule(module);
		box.size = mm2px(Vec(71.12, 128.5));
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Stellar.svg")));

		auto in = [&](float x, float y, int id) { addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, id)); };
		auto out = [&](float x, float y, int id) { addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, id)); };

		// I/O 1
		in(15.4, 18.7, Stellar::VOCT1_INPUT);
		in(30.51, 18.7, Stellar::GATE1_INPUT);
		in(45.61, 18.7, Stellar::MOD1_INPUT);
		out(60.72, 18.7, Stellar::SUB1_OUTPUT);

		// I/O 2
		in(15.4, 37.91, Stellar::VOCT2_INPUT);
		in(30.51, 37.91, Stellar::GATE2_INPUT);
		in(45.61, 37.91, Stellar::MOD2_INPUT);
		out(60.72, 37.91, Stellar::SUB2_OUTPUT);

		// AUX
		in(15.4, 57.11, Stellar::EXTIN_INPUT);
		out(30.51, 57.11, Stellar::NOISE_OUTPUT);
		out(45.61, 57.11, Stellar::MASTER_L_OUTPUT);
		out(60.72, 57.11, Stellar::MASTER_R_OUTPUT);

		// VOICE 1
		{
			struct BtnSpec { float x, y; int param, light; };
			BtnSpec btns[4] = {
				{13.5, 74.22, Stellar::AN1_PARAM, Stellar::AN1_LIGHT},
				{29.87, 74.22, Stellar::FM1_PARAM, Stellar::FM1_LIGHT},
				{46.25, 74.22, Stellar::SS1_PARAM, Stellar::SS1_LIGHT},
				{62.62, 74.22, Stellar::PL1_PARAM, Stellar::PL1_LIGHT},
			};
			for (auto& b : btns) {
				auto* btn = createParamCentered<StellarButton>(mm2px(Vec(b.x, b.y)), module, b.param);
				btn->mod = module; btn->lightId = b.light;
				btn->litColor = nvgRGB(0x8A, 0x64, 0x23);  // brass -- Voice 1 accent
				addParam(btn);
			}
			float knobY = 87.12;
			float knobX[5] = {13.5, 25.78, 38.06, 50.34, 62.62};
			int knobParams[5] = {Stellar::ATTACK1_PARAM, Stellar::DECAY1_PARAM,
			                     Stellar::SUSTAIN1_PARAM, Stellar::RELEASE1_PARAM, Stellar::CUTOFF1_PARAM};
			for (int i = 0; i < 5; i++)
				addParam(createParamCentered<StellarKnob>(mm2px(Vec(knobX[i], knobY)), module, knobParams[i]));
		}

		// VOICE 2
		{
			struct BtnSpec { float x, y; int param, light; };
			BtnSpec btns[4] = {
				{13.5, 103.42, Stellar::AN2_PARAM, Stellar::AN2_LIGHT},
				{29.87, 103.42, Stellar::FM2_PARAM, Stellar::FM2_LIGHT},
				{46.25, 103.42, Stellar::SS2_PARAM, Stellar::SS2_LIGHT},
				{62.62, 103.42, Stellar::PL2_PARAM, Stellar::PL2_LIGHT},
			};
			for (auto& b : btns) {
				auto* btn = createParamCentered<StellarButton>(mm2px(Vec(b.x, b.y)), module, b.param);
				btn->mod = module; btn->lightId = b.light;
				btn->litColor = nvgRGB(0x8A, 0x2A, 0x2A);  // maroon -- Voice 2 accent
				addParam(btn);
			}
			float knobY = 116.32;
			float knobX[5] = {13.5, 25.78, 38.06, 50.34, 62.62};
			int knobParams[5] = {Stellar::ATTACK2_PARAM, Stellar::DECAY2_PARAM,
			                     Stellar::SUSTAIN2_PARAM, Stellar::RELEASE2_PARAM, Stellar::CUTOFF2_PARAM};
			for (int i = 0; i < 5; i++)
				addParam(createParamCentered<StellarKnob>(mm2px(Vec(knobX[i], knobY)), module, knobParams[i]));
		}

		addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(5.5, 5.5)), module, Stellar::LINK_LEFT_LIGHT));
		addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(65.62, 5.5)), module, Stellar::LINK_RIGHT_LIGHT));
	}
};

Model* modelStellar = createModel<Stellar, StellarWidget>("Stellar");
