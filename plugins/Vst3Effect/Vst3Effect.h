/*
 * Vst3Effect.h - hosts VST3 effects
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

#ifndef LMMS_VST3_EFFECT_H
#define LMMS_VST3_EFFECT_H

#include <vector>

#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "SampleFrame.h"
#include "Vst3Host.h"

namespace lmms
{

class Vst3Effect;

namespace gui
{
class Vst3EffectControlDialog;
class Vst3PluginPanel;
}


class Vst3EffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit Vst3EffectControls(Vst3Effect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;
	QString nodeName() const override { return "Vst3EffectControls"; }
	int controlCount() override { return 1; }
	gui::EffectControlDialog* createView() override;

signals:
	void pluginChanged(const QString& error);

private:
	Vst3Effect* m_effect;

	friend class Vst3Effect;
	friend class gui::Vst3EffectControlDialog;
};


class Vst3Effect : public Effect
{
public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Effect() override;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

	//! Loads a module (GUI thread). Returns an error message, or an empty string on success.
	QString loadModule(const QString& path, const QString& classId = QString());

	Vst3Host& host() { return m_host; }

private:
	void setupProcessing();
	void loadState(const QDomElement& elem);

	Vst3Host m_host;
	Vst3EffectControls m_controls;
	std::vector<SampleFrame> m_wetBuffer;
	QString m_lastError;
	bool m_settingsLoaded = false;

	friend class Vst3EffectControls;
	friend class gui::Vst3EffectControlDialog;
};


namespace gui
{

class Vst3EffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit Vst3EffectControlDialog(Vst3EffectControls* controls);

private:
	Vst3PluginPanel* m_panel;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_VST3_EFFECT_H
