/*
 * Vst3Effect.cpp - hosts VST3 effects
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

#include "Vst3Effect.h"

#include <QDomElement>
#include <QTimer>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "Vst3PluginPanel.h"
#include "Vst3SubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "Host for VST3 effects"),
	"BeatStudio",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Effect)
};

}


Vst3EffectControls::Vst3EffectControls(Vst3Effect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
}


void Vst3EffectControls::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	m_effect->m_host.saveSettings(doc, elem);
}


void Vst3EffectControls::loadSettings(const QDomElement& elem)
{
	m_effect->loadState(elem);
}


gui::EffectControlDialog* Vst3EffectControls::createView()
{
	return new gui::Vst3EffectControlDialog(this);
}


Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&vst3effect_plugin_descriptor, parent, key),
	m_host(Vst3Host::Kind::Effect),
	m_controls(this)
{
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this] {
		Engine::audioEngine()->requestChangeInModel();
		setupProcessing();
		Engine::audioEngine()->doneChangeInModel();
	});

	// picked in the "Add effect" dialog: load it once construction is done, unless a project
	// load restores (possibly another) plugin through loadSettings() first
	if (key && !key->attributes.value("file").isEmpty())
	{
		setDisplayName(key->name);
		QTimer::singleShot(0, this, [this, file = key->attributes.value("file")] {
			if (!m_host.isLoaded() && !m_settingsLoaded) { loadModule(file); }
		});
	}
}


Vst3Effect::~Vst3Effect()
{
	Engine::audioEngine()->requestChangeInModel();
	m_host.unload();
	Engine::audioEngine()->doneChangeInModel();
}


Effect::ProcessStatus Vst3Effect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (frames > m_wetBuffer.size() || !m_host.process(buf, m_wetBuffer.data(), frames))
	{
		return ProcessStatus::ContinueIfNotQuiet;
	}

	const float dry = dryLevel();
	const float wet = wetLevel();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f] = buf[f] * dry + m_wetBuffer[f] * wet;
	}
	return ProcessStatus::ContinueIfNotQuiet;
}


QString Vst3Effect::loadModule(const QString& path, const QString& classId)
{
	Engine::audioEngine()->requestChangeInModel();
	QString error;
	if (m_host.load(path, classId, error))
	{
		setupProcessing();
	}
	Engine::audioEngine()->doneChangeInModel();

	if (m_host.isLoaded()) { setDisplayName(m_host.name()); }
	m_lastError = error;
	emit m_controls.pluginChanged(error);
	return error;
}


void Vst3Effect::loadState(const QDomElement& elem)
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

	if (m_host.isLoaded()) { setDisplayName(m_host.name()); }
	m_lastError = error;
	emit m_controls.pluginChanged(error);
}


void Vst3Effect::setupProcessing()
{
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	m_wetBuffer.assign(frames, SampleFrame{});
	m_host.setupProcessing(Engine::audioEngine()->outputSampleRate(), frames);
}


namespace gui
{

Vst3EffectControlDialog::Vst3EffectControlDialog(Vst3EffectControls* controls) :
	EffectControlDialog(controls),
	m_panel(new Vst3PluginPanel(controls->m_effect->host(),
		[effect = controls->m_effect](const QString& path) { return effect->loadModule(path); }, this))
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_panel);
	setMinimumSize(280, 150);

	m_panel->refresh(controls->m_effect->m_lastError);
	connect(controls, &Vst3EffectControls::pluginChanged, this,
		[this](const QString& error) { m_panel->refresh(error); });
}

} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new Vst3Effect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
