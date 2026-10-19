/*
 * Vst3Host.cpp - in-process host for a single VST3 plugin instance
 *
 * This file is part of BeatStudio (based on LMMS - https://lmms.io)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "Vst3Host.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Midi.h"
#include "MidiEvent.h"
#include "PathUtil.h"
#include "SampleFrame.h"
#include "Song.h"

#include <pluginterfaces/base/funknownimpl.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>
#include <public.sdk/source/vst/hosting/processdata.h>

namespace lmms
{

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{

constexpr int32 MaxQueuedEvents = 1024;
constexpr int32 MaxQueuedParams = 1024;
constexpr int NumMidiChannels = 16;
constexpr int NumMidiKeys = 128;

//! One host context for the whole process, as the SDK's PlugProvider expects
FUnknown* hostContext()
{
	static HostApplication* s_host = [] {
		auto host = new HostApplication; // lives for the whole process
		PluginContextFactory::instance().setPluginContext(host);
		return host;
	}();
	return s_host;
}

QByteArray readState(const std::function<tresult(IBStream*)>& getter)
{
	auto stream = owned(new MemoryStream);
	if (getter(stream) != kResultOk) { return {}; }
	return QByteArray(stream->getData(), static_cast<int>(stream->getSize()));
}

} // namespace


namespace detail
{

struct Vst3HostImpl;

//! Receives parameter edits made in the plugin's own editor
class ComponentHandler : public U::Implements<U::Directly<IComponentHandler>>
{
public:
	explicit ComponentHandler(Vst3HostImpl* impl) : m_impl(impl) {}
	void detach() { m_impl = nullptr; }

	tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override;
	tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API restartComponent(int32 flags) override;

private:
	Vst3HostImpl* m_impl;
};


class Vst3EditorWindow;

//! Lets the plugin editor ask the host to resize its window
class PlugFrame : public U::Implements<U::Directly<IPlugFrame>>
{
public:
	explicit PlugFrame(Vst3EditorWindow* window) : m_window(window) {}
	void detach() { m_window = nullptr; }
	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override;

private:
	Vst3EditorWindow* m_window;
};


//! Top-level window that the plugin's editor view is embedded into
class Vst3EditorWindow : public QWidget
{
public:
	Vst3EditorWindow(IEditController* controller, const QString& title, std::function<void()> onClosed) :
		QWidget(nullptr, Qt::Window),
		m_controller(controller),
		m_onClosed(std::move(onClosed))
	{
		setWindowTitle(title);
		setAttribute(Qt::WA_NativeWindow);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
	}

	~Vst3EditorWindow() override { detachView(); }

	bool open()
	{
		if (m_view) { show(); raise(); activateWindow(); return true; }

		m_view = owned(m_controller->createView(ViewType::kEditor));
		if (!m_view) { return false; }
#ifdef LMMS_BUILD_WIN32
		const FIDString platform = kPlatformTypeHWND;
#else
		const FIDString platform = kPlatformTypeX11EmbedWindowID;
#endif
		if (m_view->isPlatformTypeSupported(platform) != kResultTrue)
		{
			m_view = nullptr;
			return false;
		}

		m_frame = owned(new PlugFrame(this));
		m_view->setFrame(m_frame);

		// make sure the native window exists before handing it to the plugin
		const auto handle = reinterpret_cast<void*>(winId());
		if (FUnknownPtr<IPlugViewContentScaleSupport> scale{m_view})
		{
			scale->setContentScaleFactor(static_cast<IPlugViewContentScaleSupport::ScaleFactor>(devicePixelRatioF()));
		}

		ViewRect rect;
		if (m_view->getSize(&rect) == kResultOk) { applyPluginSize(rect); }

		if (m_view->attached(handle, platform) != kResultOk)
		{
			detachView();
			return false;
		}

		// some plugins only know their real size once attached
		if (m_view->getSize(&rect) == kResultOk) { applyPluginSize(rect); }

		show();
		raise();
		activateWindow();
		return true;
	}

	void detachView()
	{
		if (m_frame) { m_frame->detach(); }
		if (m_view)
		{
			m_view->removed();
			m_view->setFrame(nullptr);
			m_view = nullptr;
		}
		m_frame = nullptr;
	}

	//! Plugin asked for a new size (physical pixels)
	tresult resizeFromPlugin(IPlugView* view, ViewRect* newSize)
	{
		if (!newSize || view != m_view.get()) { return kInvalidArgument; }
		applyPluginSize(*newSize);
		view->onSize(newSize);
		return kResultTrue;
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		// destroy the view so the plugin frees its GUI resources; it is recreated on demand
		detachView();
		event->accept();
		if (m_onClosed) { m_onClosed(); }
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		if (m_applyingPluginSize || !m_view || m_view->canResize() != kResultTrue) { return; }

		const qreal dpr = devicePixelRatioF();
		ViewRect rect{0, 0,
			static_cast<int32>(std::lround(event->size().width() * dpr)),
			static_cast<int32>(std::lround(event->size().height() * dpr))};
		m_view->checkSizeConstraint(&rect);
		m_view->onSize(&rect);
	}

private:
	void applyPluginSize(const ViewRect& rect)
	{
		// VST3 sizes are in physical pixels, Qt widget sizes are in logical pixels
		const qreal dpr = devicePixelRatioF();
		const QSize size(static_cast<int>(std::lround(rect.getWidth() / dpr)),
			static_cast<int>(std::lround(rect.getHeight() / dpr)));

		m_applyingPluginSize = true;
		if (m_view && m_view->canResize() == kResultTrue)
		{
			setMinimumSize(1, 1);
			setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			resize(size);
		}
		else
		{
			setFixedSize(size);
		}
		m_applyingPluginSize = false;
	}

	IPtr<IEditController> m_controller;
	IPtr<IPlugView> m_view;
	IPtr<PlugFrame> m_frame;
	std::function<void()> m_onClosed;
	bool m_applyingPluginSize = false;
};


tresult PLUGIN_API PlugFrame::resizeView(IPlugView* view, ViewRect* newSize)
{
	return m_window ? m_window->resizeFromPlugin(view, newSize) : kResultFalse;
}


struct Vst3HostImpl
{
	explicit Vst3HostImpl(Vst3Host::Kind k) :
		kind(k),
		eventList(MaxQueuedEvents),
		inputParamChanges(MaxQueuedParams),
		outputParamChanges(MaxQueuedParams),
		toProcessor(MaxQueuedParams),
		toController(MaxQueuedParams)
	{
		pendingEvents.reserve(MaxQueuedEvents);
	}

	Vst3Host::Kind kind;

	VST3::Hosting::Module::Ptr module;
	IPtr<PlugProvider> provider;
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
	IPtr<IEditController> controller;
	IPtr<IMidiMapping> midiMapping;
	IPtr<ComponentHandler> handler;

	QString path;
	QString uid;
	QString name;
	QString vendor;

	// audio thread state
	HostProcessData processData;
	ProcessContext processContext = {};
	EventList eventList;
	ParameterChanges inputParamChanges;
	ParameterChanges outputParamChanges;
	std::vector<std::vector<Sample32>> channelBuffers;
	bool processing = false;
	sample_rate_t sampleRate = 0;
	f_cnt_t maxFrames = 0;
	int64 continuousSamples = 0;

	// cross-thread queues, guarded by queueMutex
	std::mutex queueMutex;
	std::vector<Event> pendingEvents;
	ParameterChangeTransfer toProcessor;  // editor/MIDI -> audio thread
	ParameterChangeTransfer toController; // audio thread -> editor
	std::array<std::bitset<NumMidiKeys>, NumMidiChannels> heldNotes;

	QPointer<QTimer> idleTimer;
	Vst3EditorWindow* editor = nullptr;

	bool isLoaded() const { return component && processor; }

	void queueEvent(const Event& event)
	{
		const auto lock = std::lock_guard{queueMutex};
		if (pendingEvents.size() < MaxQueuedEvents) { pendingEvents.push_back(event); }
	}

	void queueParam(ParamID id, ParamValue value, int32 offset)
	{
		const auto lock = std::lock_guard{queueMutex};
		toProcessor.addChange(id, value, offset);
	}

	// GUI thread: forward parameter changes reported by the processor to the editor
	void idle()
	{
		if (!controller) { return; }
		ParamID id;
		ParamValue value;
		int32 offset;
		const auto lock = std::lock_guard{queueMutex};
		while (toController.getNextChange(id, value, offset))
		{
			controller->setParamNormalized(id, value);
		}
	}

	bool pickClass(const QString& wantedId, VST3::Hosting::ClassInfo& out) const
	{
		const auto infos = module->getFactory().classInfos();
		const char* preferred = kind == Vst3Host::Kind::Instrument ? "Instrument" : "Fx";

		const VST3::Hosting::ClassInfo* fallback = nullptr;
		for (const auto& info : infos)
		{
			if (info.category() != kVstAudioEffectClass) { continue; }
			if (!wantedId.isEmpty())
			{
				if (QString::fromStdString(info.ID().toString()) == wantedId) { out = info; return true; }
				continue;
			}
			const auto& subs = info.subCategories();
			if (std::find(subs.begin(), subs.end(), preferred) != subs.end()) { out = info; return true; }
			if (!fallback) { fallback = &info; }
		}
		if (fallback) { out = *fallback; return true; }
		return false;
	}

	void setupBuses()
	{
		const auto numIn = component->getBusCount(kAudio, kInput);
		const auto numOut = component->getBusCount(kAudio, kOutput);

		// ask for stereo on the main buses, keep whatever the plugin has on the others
		std::vector<SpeakerArrangement> inArr(numIn), outArr(numOut);
		for (int32 i = 0; i < numIn; ++i) { processor->getBusArrangement(kInput, i, inArr[i]); }
		for (int32 i = 0; i < numOut; ++i) { processor->getBusArrangement(kOutput, i, outArr[i]); }
		if (numIn > 0) { inArr[0] = SpeakerArr::kStereo; }
		if (numOut > 0) { outArr[0] = SpeakerArr::kStereo; }
		processor->setBusArrangements(inArr.data(), numIn, outArr.data(), numOut);

		if (numIn > 0) { component->activateBus(kAudio, kInput, 0, kind == Vst3Host::Kind::Effect); }
		if (numOut > 0) { component->activateBus(kAudio, kOutput, 0, true); }
		if (component->getBusCount(kEvent, kInput) > 0) { component->activateBus(kEvent, kInput, 0, true); }
	}

	void stopProcessing()
	{
		if (!processing) { return; }
		processor->setProcessing(false);
		component->setActive(false);
		processing = false;
	}

	bool setupProcessing(sample_rate_t rate, f_cnt_t frames)
	{
		stopProcessing();

		ProcessSetup setup{kRealtime, kSample32, static_cast<int32>(frames), static_cast<SampleRate>(rate)};
		if (processor->setupProcessing(setup) != kResultOk)
		{
			qWarning() << "VST3:" << name << "rejected sample rate" << rate << "/ block size" << frames;
			return false;
		}

		sampleRate = rate;
		maxFrames = frames;

		processData.unprepare();
		processData.prepare(*component, static_cast<int32>(frames), kSample32);
		processData.inputEvents = &eventList;
		processData.inputParameterChanges = &inputParamChanges;
		processData.outputParameterChanges = &outputParamChanges;
		processData.processContext = &processContext;
		assignBuffers();

		if (component->setActive(true) != kResultOk)
		{
			qWarning() << "VST3:" << name << "failed to activate";
			return false;
		}
		processor->setProcessing(true); // many plugins return kNotImplemented here
		processing = true;
		return true;
	}

	void assignBuffers()
	{
		channelBuffers.clear();
		auto assign = [this](BusDirection dir, AudioBusBuffers* buses, int32 count) {
			for (int32 bus = 0; bus < count; ++bus)
			{
				buses[bus].silenceFlags = 0;
				for (int32 ch = 0; ch < buses[bus].numChannels; ++ch)
				{
					auto& buffer = channelBuffers.emplace_back(maxFrames, 0.f);
					processData.setChannelBuffer(dir, bus, ch, buffer.data());
				}
			}
		};
		assign(kInput, processData.inputs, processData.numInputs);
		assign(kOutput, processData.outputs, processData.numOutputs);
	}

	void fillProcessContext(f_cnt_t frames)
	{
		auto& ctx = processContext;
		ctx = {};
		ctx.sampleRate = sampleRate;
		ctx.continousTimeSamples = continuousSamples;
		ctx.state = ProcessContext::kContTimeValid;

		if (auto song = Engine::getSong())
		{
			const auto& timeSig = song->getTimeSigModel();
			const int num = std::max(1, timeSig.getNumerator());
			const int den = std::max(1, timeSig.getDenominator());
			const double tempo = std::max(1.0, static_cast<double>(song->getTempo()));
			const double ticks = song->getTicks();
			// LMMS has DefaultTicksPerBar ticks per 4/4 bar, i.e. a quarter note is a quarter of that
			const double quarters = ticks * 4.0 / DefaultTicksPerBar;
			const double quartersPerBar = num * 4.0 / den;

			ctx.tempo = tempo;
			ctx.timeSigNumerator = num;
			ctx.timeSigDenominator = den;
			ctx.projectTimeMusic = quarters;
			ctx.barPositionMusic = std::floor(quarters / quartersPerBar) * quartersPerBar;
			ctx.projectTimeSamples = static_cast<TSamples>(quarters * 60.0 / tempo * sampleRate);
			ctx.state |= ProcessContext::kTempoValid | ProcessContext::kTimeSigValid
				| ProcessContext::kProjectTimeMusicValid | ProcessContext::kBarPositionValid;
			if (song->isPlaying()) { ctx.state |= ProcessContext::kPlaying; }
		}
		(void) frames;
	}
};


tresult PLUGIN_API ComponentHandler::performEdit(ParamID id, ParamValue value)
{
	if (m_impl) { m_impl->queueParam(id, value, 0); }
	return kResultOk;
}


tresult PLUGIN_API ComponentHandler::restartComponent(int32 flags)
{
	if (!m_impl) { return kResultFalse; }
	if (flags & (kIoChanged | kLatencyChanged | kReloadComponent))
	{
		// the bus layout or processing setup changed: redo the setup on the GUI thread
		// (the plugin may call this from any thread, and must not be re-set-up from inside it)
		QTimer::singleShot(0, qApp, [self = IPtr<ComponentHandler>(this)] {
			auto impl = self->m_impl;
			if (!impl || !impl->isLoaded() || !impl->sampleRate) { return; }
			Engine::audioEngine()->requestChangeInModel();
			impl->stopProcessing();
			impl->setupBuses();
			impl->setupProcessing(impl->sampleRate, impl->maxFrames);
			Engine::audioEngine()->doneChangeInModel();
		});
	}
	return kResultOk;
}

} // namespace detail


using detail::Vst3HostImpl;


Vst3Host::Vst3Host(Kind kind) :
	m_impl(std::make_unique<Vst3HostImpl>(kind))
{
}


Vst3Host::~Vst3Host()
{
	unload();
}


QStringList Vst3Host::installedModules()
{
	QStringList result;
	for (const auto& path : VST3::Hosting::Module::getModulePaths())
	{
		result << QString::fromStdString(path);
	}
	result.removeDuplicates();
	result.sort(Qt::CaseInsensitive);
	return result;
}


bool Vst3Host::load(const QString& modulePath, const QString& classId, QString& error)
{
	unload();
	auto& d = *m_impl;
	hostContext();

	std::string err;
	d.module = VST3::Hosting::Module::create(modulePath.toStdString(), err);
	if (!d.module)
	{
		error = QObject::tr("Could not load VST3 module \"%1\": %2")
			.arg(modulePath, QString::fromStdString(err));
		return false;
	}

	VST3::Hosting::ClassInfo info;
	if (!d.pickClass(classId, info))
	{
		error = QObject::tr("\"%1\" doesn't contain a usable VST3 audio processor.").arg(modulePath);
		unload();
		return false;
	}

	d.provider = owned(new PlugProvider(d.module->getFactory(), info, true));
	if (!d.provider->initialize())
	{
		error = QObject::tr("The VST3 plugin \"%1\" failed to initialize.")
			.arg(QString::fromStdString(info.name()));
		unload();
		return false;
	}

	d.component = d.provider->getComponentPtr();
	d.controller = d.provider->getControllerPtr();
	d.processor = U::cast<IAudioProcessor>(d.component);
	if (!d.component || !d.processor)
	{
		error = QObject::tr("The VST3 plugin \"%1\" has no audio processor.")
			.arg(QString::fromStdString(info.name()));
		unload();
		return false;
	}
	if (d.processor->canProcessSampleSize(kSample32) != kResultTrue)
	{
		error = QObject::tr("The VST3 plugin \"%1\" doesn't support 32-bit float processing.")
			.arg(QString::fromStdString(info.name()));
		unload();
		return false;
	}

	if (d.controller)
	{
		d.handler = owned(new detail::ComponentHandler(&d));
		d.controller->setComponentHandler(d.handler);
		d.midiMapping = U::cast<IMidiMapping>(d.controller);
	}

	d.path = modulePath;
	d.uid = QString::fromStdString(info.ID().toString());
	d.name = QString::fromStdString(info.name());
	d.vendor = QString::fromStdString(info.vendor());
	for (auto& channel : d.heldNotes) { channel.reset(); }

	d.setupBuses();

	d.idleTimer = new QTimer;
	QObject::connect(d.idleTimer, &QTimer::timeout, d.idleTimer, [impl = m_impl.get()] { impl->idle(); });
	d.idleTimer->start(30);

	return true;
}


void Vst3Host::unload()
{
	auto& d = *m_impl;

	if (d.editor)
	{
		delete d.editor;
		d.editor = nullptr;
	}
	if (d.idleTimer) { delete d.idleTimer; }

	if (d.isLoaded()) { d.stopProcessing(); }
	if (d.controller) { d.controller->setComponentHandler(nullptr); }
	if (d.handler) { d.handler->detach(); }
	d.processData.unprepare();
	d.channelBuffers.clear();

	d.handler = nullptr;
	d.midiMapping = nullptr;
	d.processor = nullptr;
	d.controller = nullptr;
	d.component = nullptr;
	d.provider = nullptr; // terminates and disconnects the plugin
	d.module = nullptr;   // unloads the library

	d.processing = false;
	d.sampleRate = 0;
	d.maxFrames = 0;
	d.continuousSamples = 0;
	d.path.clear();
	d.uid.clear();
	d.name.clear();
	d.vendor.clear();

	const auto lock = std::lock_guard{d.queueMutex};
	d.pendingEvents.clear();
	d.toProcessor.removeChanges();
	d.toController.removeChanges();
}


bool Vst3Host::isLoaded() const { return m_impl->isLoaded(); }
QString Vst3Host::modulePath() const { return m_impl->path; }
QString Vst3Host::classId() const { return m_impl->uid; }
QString Vst3Host::name() const { return m_impl->name; }
QString Vst3Host::vendor() const { return m_impl->vendor; }


void Vst3Host::setupProcessing(sample_rate_t sampleRate, f_cnt_t maxFrames)
{
	if (m_impl->isLoaded()) { m_impl->setupProcessing(sampleRate, maxFrames); }
}


bool Vst3Host::process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames)
{
	auto& d = *m_impl;
	if (!d.processing || frames == 0 || frames > d.maxFrames) { return false; }
	auto& data = d.processData;

	// inputs: main bus gets the LMMS signal (effects), everything else silence
	for (int32 bus = 0; bus < data.numInputs; ++bus)
	{
		auto& buffers = data.inputs[bus];
		for (int32 ch = 0; ch < buffers.numChannels; ++ch)
		{
			Sample32* dst = buffers.channelBuffers32[ch];
			if (bus == 0 && in && ch < 2)
			{
				for (f_cnt_t f = 0; f < frames; ++f) { dst[f] = in[f][ch]; }
			}
			else
			{
				std::fill_n(dst, frames, 0.f);
			}
		}
	}

	// pick up events and parameter changes queued by other threads; if the queue is busy
	// they simply arrive one period later
	if (auto lock = std::unique_lock{d.queueMutex, std::try_to_lock})
	{
		for (auto event : d.pendingEvents)
		{
			event.sampleOffset = std::clamp<int32>(event.sampleOffset, 0, static_cast<int32>(frames) - 1);
			d.eventList.addEvent(event);
		}
		d.pendingEvents.clear();
		d.toProcessor.transferChangesTo(d.inputParamChanges);
	}

	d.fillProcessContext(frames);
	data.numSamples = static_cast<int32>(frames);

	const bool ok = d.processor->process(data) == kResultOk;

	d.eventList.clear();
	d.inputParamChanges.clearQueue();
	if (d.outputParamChanges.getParameterCount() > 0)
	{
		if (auto lock = std::unique_lock{d.queueMutex, std::try_to_lock})
		{
			d.toController.transferChangesFrom(d.outputParamChanges);
		}
		d.outputParamChanges.clearQueue();
	}
	d.continuousSamples += static_cast<int64>(frames);

	if (!ok || data.numOutputs < 1 || data.outputs[0].numChannels < 1) { return false; }

	const auto& main = data.outputs[0];
	const Sample32* left = main.channelBuffers32[0];
	const Sample32* right = main.numChannels > 1 ? main.channelBuffers32[1] : left;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		out[f][0] = left[f];
		out[f][1] = right[f];
	}
	return true;
}


void Vst3Host::queueMidi(const MidiEvent& event, f_cnt_t offset)
{
	auto& d = *m_impl;
	if (!d.isLoaded()) { return; }

	const int channel = std::clamp<int>(event.channel(), 0, NumMidiChannels - 1);
	const auto sampleOffset = static_cast<int32>(offset);

	auto mapController = [&](CtrlNumber ctrl, ParamValue value) {
		ParamID id;
		if (d.midiMapping && d.midiMapping->getMidiControllerAssignment(0, static_cast<int16>(channel), ctrl, id) == kResultTrue)
		{
			d.queueParam(id, value, sampleOffset);
		}
	};

	Event e = {};
	e.busIndex = 0;
	e.sampleOffset = sampleOffset;

	switch (event.type())
	{
	case MidiNoteOn:
	case MidiNoteOff:
	{
		const int key = event.key();
		if (key < 0 || key >= NumMidiKeys) { return; }
		const bool on = event.type() == MidiNoteOn && event.velocity() > 0;
		if (on)
		{
			e.type = Event::kNoteOnEvent;
			e.noteOn = {static_cast<int16>(channel), static_cast<int16>(key), 0.f, event.velocity() / 127.f, 0, -1};
		}
		else
		{
			e.type = Event::kNoteOffEvent;
			e.noteOff = {static_cast<int16>(channel), static_cast<int16>(key), event.velocity() / 127.f, -1, 0.f};
		}
		{
			const auto lock = std::lock_guard{d.queueMutex};
			d.heldNotes[channel].set(key, on);
		}
		d.queueEvent(e);
		break;
	}
	case MidiKeyPressure:
	{
		const int key = event.key();
		if (key < 0 || key >= NumMidiKeys) { return; }
		e.type = Event::kPolyPressureEvent;
		e.polyPressure = {static_cast<int16>(channel), static_cast<int16>(key), event.velocity() / 127.f, -1};
		d.queueEvent(e);
		break;
	}
	case MidiControlChange:
		if (event.controllerNumber() == MidiControllerAllNotesOff
			|| event.controllerNumber() == MidiControllerAllSoundOff)
		{
			allNotesOff();
		}
		mapController(static_cast<CtrlNumber>(event.controllerNumber()), event.controllerValue() / 127.0);
		break;
	case MidiPitchBend:
		mapController(kPitchBend, std::clamp(event.pitchBend() / static_cast<double>(MidiMaxPitchBend), 0.0, 1.0));
		break;
	case MidiChannelPressure:
		mapController(kAfterTouch, event.channelPressure() / 127.0);
		break;
	default:
		break;
	}
}


void Vst3Host::allNotesOff()
{
	auto& d = *m_impl;
	const auto lock = std::lock_guard{d.queueMutex};
	for (int channel = 0; channel < NumMidiChannels; ++channel)
	{
		for (int key = 0; key < NumMidiKeys; ++key)
		{
			if (!d.heldNotes[channel].test(key)) { continue; }
			Event e = {};
			e.type = Event::kNoteOffEvent;
			e.noteOff = {static_cast<int16>(channel), static_cast<int16>(key), 0.f, -1, 0.f};
			if (d.pendingEvents.size() < MaxQueuedEvents) { d.pendingEvents.push_back(e); }
		}
		d.heldNotes[channel].reset();
	}
}


void Vst3Host::saveSettings(QDomDocument& doc, QDomElement& elem) const
{
	auto& d = *m_impl;
	if (!d.isLoaded()) { return; }

	elem.setAttribute("vst3path", PathUtil::toShortestRelative(d.path));
	elem.setAttribute("vst3class", d.uid);
	elem.setAttribute("vst3name", d.name);

	const auto componentState = readState([&](IBStream* s) { return d.component->getState(s); });
	if (!componentState.isEmpty())
	{
		auto node = doc.createElement("componentstate");
		node.appendChild(doc.createTextNode(QString::fromLatin1(componentState.toBase64())));
		elem.appendChild(node);
	}
	if (d.controller)
	{
		const auto controllerState = readState([&](IBStream* s) { return d.controller->getState(s); });
		if (!controllerState.isEmpty())
		{
			auto node = doc.createElement("controllerstate");
			node.appendChild(doc.createTextNode(QString::fromLatin1(controllerState.toBase64())));
			elem.appendChild(node);
		}
	}
}


bool Vst3Host::loadSettings(const QDomElement& elem, QString& error)
{
	const QString path = elem.attribute("vst3path");
	if (path.isEmpty()) { unload(); return true; } // empty slot

	auto& d = *m_impl;
	const QString absolutePath = PathUtil::toAbsolute(path);
	const QString wantedClass = elem.attribute("vst3class");
	const bool alreadyLoaded = d.isLoaded() && QFileInfo(d.path) == QFileInfo(absolutePath)
		&& (wantedClass.isEmpty() || wantedClass == d.uid);
	if (!alreadyLoaded && !load(absolutePath, wantedClass, error)) { return false; }

	auto stateFrom = [&](const char* tag) {
		return QByteArray::fromBase64(elem.firstChildElement(tag).text().toLatin1());
	};

	auto componentState = stateFrom("componentstate");
	if (!componentState.isEmpty())
	{
		auto stream = owned(new MemoryStream(componentState.data(), componentState.size()));
		d.component->setState(stream);
		if (d.controller)
		{
			int64 pos;
			stream->seek(0, IBStream::kIBSeekSet, &pos);
			d.controller->setComponentState(stream);
		}
	}

	auto controllerState = stateFrom("controllerstate");
	if (d.controller && !controllerState.isEmpty())
	{
		auto stream = owned(new MemoryStream(controllerState.data(), controllerState.size()));
		d.controller->setState(stream);
	}
	return true;
}


bool Vst3Host::hasEditor() const
{
	return m_impl->controller != nullptr;
}


void Vst3Host::showEditor(QWidget* parentForPosition)
{
	auto& d = *m_impl;
	if (!d.controller) { return; }

	if (!d.editor)
	{
		d.editor = new detail::Vst3EditorWindow(d.controller, d.name, [this] {
			if (onEditorClosed) { onEditorClosed(); }
		});
		if (parentForPosition)
		{
			d.editor->move(parentForPosition->mapToGlobal(QPoint(parentForPosition->width() + 10, 0)));
		}
	}
	if (!d.editor->open())
	{
		qWarning() << "VST3:" << d.name << "could not open its editor";
	}
}


void Vst3Host::hideEditor()
{
	if (auto editor = m_impl->editor; editor && editor->isVisible())
	{
		editor->close();
	}
}


bool Vst3Host::isEditorVisible() const
{
	return m_impl->editor && m_impl->editor->isVisible();
}

} // namespace lmms
