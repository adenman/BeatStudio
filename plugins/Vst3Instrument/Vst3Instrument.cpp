/*
 * Vst3Instrument.cpp - hosts VST3 instruments
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

#include "Vst3Instrument.h"

#include <QDebug>
#include <QDomElement>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Clipboard.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "StringPairDrag.h"
#include "Vst3PluginPanel.h"
#include "Vst3SubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3instrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "Host for VST3 instruments"),
	"BeatStudio",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	"vst3",
	new Vst3SubPluginFeatures(Plugin::Type::Instrument)
};

}


Vst3Instrument::Vst3Instrument(InstrumentTrack* track, Descriptor::SubPluginFeatures::Key* key) :
	Instrument(track, &vst3instrument_plugin_descriptor, key, Flag::IsSingleStreamed | Flag::IsMidiBased),
	m_host(Vst3Host::Kind::Instrument)
{
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this] {
		Engine::audioEngine()->requestChangeInModel();
		setupProcessing();
		Engine::audioEngine()->doneChangeInModel();
	});

	// picked from the plugin browser: load it once construction is done, unless a project
	// load restores (possibly another) plugin through loadSettings() first
	if (key && !key->attributes["file"].isEmpty())
	{
		QTimer::singleShot(0, this, [this, file = key->attributes["file"]] {
			if (!m_host.isLoaded() && !m_settingsLoaded) { loadModule(file); }
		});
	}

	// the play handle calls play() once per period
	auto iph = new InstrumentPlayHandle(this, track);
	Engine::audioEngine()->addPlayHandle(iph);
}


Vst3Instrument::~Vst3Instrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes(instrumentTrack(),
		PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);

	Engine::audioEngine()->requestChangeInModel();
	m_host.unload();
	Engine::audioEngine()->doneChangeInModel();
}


void Vst3Instrument::play(SampleFrame* workingBuffer)
{
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	if (!m_host.process(nullptr, workingBuffer, frames))
	{
		std::fill_n(workingBuffer, frames, SampleFrame{});
	}
}


bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	(void) time;
	m_host.queueMidi(event, offset);
	return true;
}


void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	m_host.saveSettings(doc, elem);
}


void Vst3Instrument::loadSettings(const QDomElement& elem)
{
	m_settingsLoaded = true;
	Engine::audioEngine()->requestChangeInModel();
	QString error;
	if (!m_host.loadSettings(elem, error))
	{
		collectErrorForUI(error);
	}
	setupProcessing();
	Engine::audioEngine()->doneChangeInModel();

	// keep the track name the user saved with the project
	m_lastError = error;
	emit pluginChanged(error);
}


void Vst3Instrument::loadFile(const QString& file)
{
	loadModule(file);
}


QString Vst3Instrument::nodeName() const
{
	return vst3instrument_plugin_descriptor.name;
}


QString Vst3Instrument::loadModule(const QString& path, const QString& classId)
{
	Engine::audioEngine()->requestChangeInModel();
	QString error;
	if (m_host.load(path, classId, error))
	{
		setupProcessing();
	}
	Engine::audioEngine()->doneChangeInModel();

	m_lastError = error;
	updateDisplayName();
	emit pluginChanged(error);
	return error;
}


void Vst3Instrument::setupProcessing()
{
	m_host.setupProcessing(Engine::audioEngine()->outputSampleRate(), Engine::audioEngine()->framesPerPeriod());
}


void Vst3Instrument::updateDisplayName()
{
	if (m_host.isLoaded())
	{
		instrumentTrack()->setName(m_host.name());
	}
}


gui::PluginView* Vst3Instrument::instantiateView(QWidget* parent)
{
	return new gui::Vst3InstrumentView(this, parent);
}


namespace gui
{

Vst3InstrumentView::Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent) :
	InstrumentView(instrument, parent),
	m_panel(new Vst3PluginPanel(instrument->host(),
		[instrument](const QString& path) { return instrument->loadModule(path); }, this))
{
	setAutoFillBackground(true);
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_panel);
	m_panel->refresh(instrument->m_lastError);
}


void Vst3InstrumentView::modelChanged()
{
	auto instrument = castModel<Vst3Instrument>();
	connect(instrument, &Vst3Instrument::pluginChanged, this,
		[this](const QString& error) { m_panel->refresh(error); }, Qt::UniqueConnection);
}


void Vst3InstrumentView::dragEnterEvent(QDragEnterEvent* event)
{
	using namespace Clipboard;
	if (event->mimeData()->hasFormat(mimeType(MimeType::StringPair)))
	{
		const QString text = event->mimeData()->data(mimeType(MimeType::StringPair));
		if (text.section(':', 0, 0) == "vstpluginfile" && text.endsWith(".vst3", Qt::CaseInsensitive))
		{
			event->acceptProposedAction();
			return;
		}
	}
	event->ignore();
}


void Vst3InstrumentView::dropEvent(QDropEvent* event)
{
	if (StringPairDrag::decodeKey(event) == "vstpluginfile")
	{
		castModel<Vst3Instrument>()->loadModule(StringPairDrag::decodeValue(event));
		event->accept();
		return;
	}
	event->ignore();
}

} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new Vst3Instrument(static_cast<InstrumentTrack*>(parent),
		static_cast<Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
